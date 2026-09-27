# Pocketful — Stage 2 Browser UI

Node.js, zero external dependencies. Requires the Stage 1 API running on port 8080.

Build and run (with docker-compose from the Bundle root):

```sh
docker compose -f agent/docker-compose.test.yml up --build
```

Or stand up the UI alone (API must already be running on port 8080):

```sh
docker build -t pocketful-stage-2 .
docker run --rm -p 3000:3000 pocketful-stage-2
```

Then open `http://localhost:3000` in a browser.

## Notes

- Serves the browser client at port 3000 (configurable via `PORT`).
- The browser client makes API calls directly to `http://localhost:8080` by default.
- All state is client-side (localStorage for auth token and idempotency keys).
- No build step required — vanilla HTML/JS, no bundler.
