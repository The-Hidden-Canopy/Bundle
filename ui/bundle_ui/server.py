"""Local-only HTTP server for the Bundle interface.

Binds to 127.0.0.1, rejects requests whose Host header is not this loopback
address (DNS-rebinding guard), and requires a per-session token on every /api
call (the token is embedded in the page it serves, so other web pages cannot
call the API). Static files are served from ./static.
"""

from __future__ import annotations

import json
import secrets
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable
from urllib.parse import parse_qs, urlparse

from .backend import BackendError, BundleBackend

STATIC = Path(__file__).resolve().parent / "static"
# Synthetic sample statement shipped with the repository (no real records).
SAMPLE_CSV = Path(__file__).resolve().parents[2] / "examples" / "native-sqlite" / "statement.csv"
CONTENT_TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
                 ".js": "text/javascript; charset=utf-8", ".svg": "image/svg+xml"}
MAX_BODY = 12 * 1024 * 1024


def _int(value: Any, name: str) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        raise BackendError(f"{name} must be a whole number.") from None


class Api:
    def __init__(self, backend: BundleBackend) -> None:
        self.backend = backend

    def state(self, query: dict[str, str], _body: dict[str, Any]) -> dict[str, Any]:
        snapshot = self.backend.snapshot()
        return {"workspace": self.backend.workspace_info(), "snapshot": snapshot,
                "saved_floor": self.backend.saved_floor(snapshot)}

    def calculate(self, query: dict[str, str], body: dict[str, Any]) -> dict[str, Any]:
        source = body or query
        return self.backend.calculate(
            mode=str(source.get("mode", "expected")),
            horizon_days=_int(source.get("horizon_days", 14), "horizon_days"),
            floor_minor=_int(source.get("floor_minor", 0), "floor_minor"),
            record=bool(body.get("record", False)) if body else False,
            what_if=body.get("what_if") if body else None,
        )

    def history(self, query: dict[str, str], _body: dict[str, Any]) -> list[dict[str, Any]]:
        return self.backend.history(_int(query.get("limit", 100), "limit"))

    def import_preview(self, _query: dict[str, str], body: dict[str, Any]) -> dict[str, Any]:
        return self.backend.preview_import(str(body.get("filename", "")), str(body.get("content", "")),
                                           source_id=str(body.get("source_id", "")),
                                           statement_utc_offset=str(body.get("utc_offset", "")))

    def import_commit(self, _query: dict[str, str], body: dict[str, Any]) -> dict[str, Any]:
        return self.backend.commit_import(str(body.get("preview_id", "")))

    def backup_export(self, _query: dict[str, str], _body: dict[str, Any]) -> dict[str, Any]:
        return self.backend.export_backup()

    def backup_verify(self, _query: dict[str, str], body: dict[str, Any]) -> dict[str, Any]:
        return self.backend.verify_backup(str(body.get("path", "")))

    def backup_restore(self, _query: dict[str, str], body: dict[str, Any]) -> dict[str, Any]:
        return self.backend.restore_backup(str(body.get("path", "")))

    def demo_start(self, _query: dict[str, str], _body: dict[str, Any]) -> dict[str, Any]:
        return self.backend.use_demo()

    def demo_exit(self, _query: dict[str, str], _body: dict[str, Any]) -> dict[str, Any]:
        return self.backend.use_user_data()


ROUTES: dict[tuple[str, str], Callable[[Api, dict[str, str], dict[str, Any]], Any]] = {
    ("GET", "/api/state"): Api.state,
    ("GET", "/api/calculate"): Api.calculate,
    ("POST", "/api/calculate"): Api.calculate,
    ("GET", "/api/history"): Api.history,
    ("POST", "/api/import/preview"): Api.import_preview,
    ("POST", "/api/import/commit"): Api.import_commit,
    ("POST", "/api/backup/export"): Api.backup_export,
    ("POST", "/api/backup/verify"): Api.backup_verify,
    ("POST", "/api/backup/restore"): Api.backup_restore,
    ("POST", "/api/demo/start"): Api.demo_start,
    ("POST", "/api/demo/exit"): Api.demo_exit,
}


def make_server(backend: BundleBackend, port: int = 0) -> tuple[ThreadingHTTPServer, str]:
    token = secrets.token_urlsafe(24)
    api = Api(backend)

    class Handler(BaseHTTPRequestHandler):
        server_version = "BundleLocal/1"

        def log_message(self, *args: Any) -> None:  # no request logging (amounts can appear in URLs)
            pass

        def _allowed_host(self) -> bool:
            host = self.headers.get("Host", "")
            port_text = str(self.server.server_address[1])
            return host in (f"127.0.0.1:{port_text}", f"localhost:{port_text}")

        def _send(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy",
                             "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; "
                             "connect-src 'self'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, status: int, payload: Any) -> None:
            self._send(status, json.dumps(payload, ensure_ascii=False).encode("utf-8"), "application/json")

        def _handle(self, method: str) -> None:
            if not self._allowed_host():
                self._json(HTTPStatus.FORBIDDEN, {"error": "Bundle only answers on its local address."})
                return
            url = urlparse(self.path)
            if not url.path.startswith("/api/"):
                if method != "GET":
                    self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "method not allowed"})
                    return
                self._static(url.path)
                return
            if not secrets.compare_digest(self.headers.get("X-Bundle-Token", ""), token):
                self._json(HTTPStatus.FORBIDDEN, {"error": "Missing or wrong session token. Reload the page."})
                return
            handler = ROUTES.get((method, url.path))
            if handler is None:
                self._json(HTTPStatus.NOT_FOUND, {"error": "Unknown request."})
                return
            body: dict[str, Any] = {}
            if method == "POST":
                length = int(self.headers.get("Content-Length") or 0)
                if length > MAX_BODY:
                    self._json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"error": "Request is too large."})
                    return
                try:
                    body = json.loads(self.rfile.read(length) or b"{}")
                except json.JSONDecodeError:
                    self._json(HTTPStatus.BAD_REQUEST, {"error": "Request body is not JSON."})
                    return
            query = {key: values[-1] for key, values in parse_qs(url.query).items()}
            try:
                self._json(HTTPStatus.OK, handler(api, query, body))
            except BackendError as error:
                self._json(error.status, error.to_dict())
            except Exception as error:  # noqa: BLE001 -- never leak a stack trace to the page
                self._json(HTTPStatus.INTERNAL_SERVER_ERROR,
                           {"error": f"Unexpected error: {type(error).__name__}",
                            "recovery": "Nothing was changed by this request. Try again or restart Bundle."})

        def _static(self, path: str) -> None:
            if path == "/sample/statement.csv":
                if not SAMPLE_CSV.is_file():
                    self._json(HTTPStatus.NOT_FOUND, {"error": "The sample statement is not in this checkout."})
                    return
                self._send(HTTPStatus.OK, SAMPLE_CSV.read_bytes(), "text/csv; charset=utf-8")
                return
            name = "index.html" if path in ("/", "/index.html") else path.lstrip("/")
            target = (STATIC / name).resolve()
            if STATIC not in target.parents or not target.is_file():
                self._json(HTTPStatus.NOT_FOUND, {"error": "Not found."})
                return
            data = target.read_bytes()
            if target.name == "index.html":
                data = data.replace(b"__BUNDLE_SESSION_TOKEN__", token.encode("ascii"))
            self._send(HTTPStatus.OK, data, CONTENT_TYPES.get(target.suffix, "application/octet-stream"))

        def do_GET(self) -> None:  # noqa: N802
            self._handle("GET")

        def do_POST(self) -> None:  # noqa: N802
            self._handle("POST")

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    return server, token


def serve_in_thread(backend: BundleBackend, port: int = 0) -> tuple[ThreadingHTTPServer, str, str]:
    server, token = make_server(backend, port)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, token, f"http://127.0.0.1:{server.server_address[1]}/"
