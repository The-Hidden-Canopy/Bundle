'use strict';

const http = require('http');
const { URL } = require('url');

const store = require('./lib/store');
const { ApiError, errors } = require('./lib/http-helpers');
const h = require('./lib/handlers');

const PORT = parseInt(process.env.PORT, 10) || 8080;
const MAX_BODY_BYTES = 5 * 1024 * 1024;
const REQUEST_TIMEOUT_MS = 5_000;
const TEST_CONTROL_TIMEOUT_MS = 10_000;
const PUBLIC_PATHS = new Set(['/health', '/_test/reset', '/_test/export', '/_test/import', '/auth/signup', '/auth/login']);

const routes = [
  { method: 'GET', pattern: /^\/health$/, params: [], handler: () => ({ status: 200, body: { status: 'ok' } }) },
  { method: 'POST', pattern: /^\/_test\/reset$/, params: [], handler: h.testReset },
  { method: 'GET', pattern: /^\/_test\/export$/, params: [], handler: h.testExport },
  { method: 'POST', pattern: /^\/_test\/import$/, params: [], handler: h.testImport },
  { method: 'POST', pattern: /^\/auth\/signup$/, params: [], handler: h.signup },
  { method: 'POST', pattern: /^\/auth\/login$/, params: [], handler: h.login },
  { method: 'GET', pattern: /^\/me$/, params: [], handler: h.me },
  { method: 'POST', pattern: /^\/payments$/, params: [], handler: h.createPayments },
  { method: 'POST', pattern: /^\/requests$/, params: [], handler: h.createRequest },
  { method: 'GET', pattern: /^\/requests$/, params: [], handler: h.listRequests },
  { method: 'POST', pattern: /^\/requests\/([^/]+)\/pay$/, params: ['id'], handler: h.payRequest },
  { method: 'POST', pattern: /^\/requests\/([^/]+)\/decline$/, params: ['id'], handler: h.declineRequest },
  { method: 'POST', pattern: /^\/requests\/([^/]+)\/cancel$/, params: ['id'], handler: h.cancelRequest },
  { method: 'POST', pattern: /^\/splits$/, params: [], handler: h.createSplit },
  { method: 'GET', pattern: /^\/activity$/, params: [], handler: h.listActivity },
  { method: 'POST', pattern: /^\/settlements$/, params: [], handler: h.createSettlement },
];

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    req.on('data', (chunk) => {
      total += chunk.length;
      if (total > MAX_BODY_BYTES) {
        reject(errors.malformed('body too large'));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', () => reject(errors.malformed('error reading body')));
  });
}

function parseBody(raw) {
  if (raw.length === 0) return {};
  let parsed;
  try { parsed = JSON.parse(raw); } catch (e) { throw errors.malformed('body is not valid JSON'); }
  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) throw errors.malformed('body must be a JSON object');
  return parsed;
}

function parseQuery(url) {
  const out = {};
  for (const [k, v] of url.searchParams.entries()) out[k] = v;
  return out;
}

function send(res, status, body) {
  if (body === null || body === undefined) {
    res.writeHead(status, { 'Content-Length': '0' });
    res.end();
    return;
  }
  const payload = Buffer.from(JSON.stringify(body), 'utf8');
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': String(payload.length) });
  res.end(payload);
}

function sendError(res, apiErr) { send(res, apiErr.status, { error: { code: apiErr.code, message: apiErr.message } }); }

function startDeadline(req, res, timeoutMs) {
  let expired = false;
  const timer = setTimeout(() => {
    expired = true;
    if (!res.writableEnded && !res.destroyed) {
      res.writeHead(408, { Connection: 'close', 'Content-Length': '0' });
      res.end();
    }
    req.destroy();
  }, timeoutMs);
  return { clear: () => clearTimeout(timer), expired: () => expired };
}

async function handleRequest(req, res) {
  let url;
  try { url = new URL(req.url, 'http://internal'); } catch (e) { return sendError(res, errors.malformed('invalid request URL')); }
  const pathname = url.pathname.length > 1 ? url.pathname.replace(/\/+$/, '') : url.pathname;
  const method = req.method;
  const match = routes.find((r) => r.method === method && r.pattern.test(pathname));
  if (!match) return sendError(res, errors.notFound());
  const timeoutMs = pathname === '/_test/reset' || pathname === '/_test/import'
    ? TEST_CONTROL_TIMEOUT_MS
    : REQUEST_TIMEOUT_MS;
  const deadline = startDeadline(req, res, timeoutMs);
  try {
    const rawBody = await readBody(req);
    let body = {};
    if (method === 'POST') body = parseBody(rawBody);
    const headers = {};
    for (const [k, v] of Object.entries(req.headers)) headers[k.toLowerCase()] = v;
    const user = PUBLIC_PATHS.has(pathname) ? null : h.authenticate(headers);
    const groups = match.pattern.exec(pathname);
    const params = {};
    match.params.forEach((name, index) => { params[name] = groups[index + 1]; });
    const result = match.handler({ method, path: pathname, params, query: parseQuery(url), headers, body, user });
    send(res, result.status, result.body);
  } catch (e) {
    if (deadline.expired() || res.writableEnded || res.destroyed) return;
    if (e instanceof ApiError) return sendError(res, e);
    console.error('unexpected error', e);
    return sendError(res, errors.malformed('unable to process request'));
  } finally {
    deadline.clear();
  }
}

const server = http.createServer((req, res) => {
  handleRequest(req, res).catch((error) => {
    console.error('unhandled error', error);
    try { sendError(res, errors.malformed('unable to process request')); } catch (e) { res.end(); }
  });
});

// Node's parser has a 10 s upper bound; startDeadline narrows ordinary requests to 5 s
// after their route is known while preserving the documented 10 s test-control budget.
server.requestTimeout = TEST_CONTROL_TIMEOUT_MS;
server.keepAliveTimeout = 60000;
server.maxConnections = 1024;
server.listen(PORT, '0.0.0.0', () => console.log(`pocketful stage-1 listening on 0.0.0.0:${PORT}`));

module.exports = { server, store };
