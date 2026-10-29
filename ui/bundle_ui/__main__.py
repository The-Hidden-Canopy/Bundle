"""Start the local Bundle interface.

    python -m bundle_ui --db ~/bundle/finance.db [--port 8765] [--open]

Run from the repository with PYTHONPATH=ui. The database file is created on
first use and is a plain local SQLite file (not encrypted).
"""

from __future__ import annotations

import argparse
import webbrowser
from pathlib import Path

from .backend import BackendError, BundleBackend
from .server import make_server


def main() -> int:
    parser = argparse.ArgumentParser(prog="bundle_ui", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--db", required=True, type=Path, help="local database file (created if missing)")
    parser.add_argument("--port", type=int, default=8765, help="local port (default 8765; 0 picks a free one)")
    parser.add_argument("--native", type=Path, help="path to the bundle executable (default: build output)")
    parser.add_argument("--open", action="store_true", help="open the interface in the default browser")
    args = parser.parse_args()
    try:
        backend = BundleBackend(args.db.expanduser(), native_cli=args.native)
        backend._run("init", "--db", backend.user.database)
    except BackendError as error:
        parser.exit(2, f"bundle_ui: {error.message}\n{error.recovery}\n")
    server, _token = make_server(backend, args.port)
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"Bundle is running at {url} (local only). Database: {backend.user.database}")
    print("Press Ctrl+C to stop. Your data stays in the database file above.")
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
