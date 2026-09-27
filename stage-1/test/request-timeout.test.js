'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const http = require('node:http');
const net = require('node:net');
const path = require('node:path');
const { spawn } = require('node:child_process');
const test = require('node:test');

async function reservePort() {
  const listener = net.createServer();
  listener.listen(0, '127.0.0.1');
  await once(listener, 'listening');
  const { port } = listener.address();
  await new Promise((resolve, reject) => listener.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function startServer(port) {
  const child = spawn(process.execPath, ['server.js'], {
    cwd: path.join(__dirname, '..'),
    env: { ...process.env, PORT: String(port) },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stdout = '';
  child.stdout.on('data', (chunk) => { stdout += chunk; });
  const deadline = Date.now() + 2_000;
  while (!stdout.includes('listening on')) {
    if (child.exitCode !== null) throw new Error(`server exited before listening: ${stdout}`);
    if (Date.now() >= deadline) throw new Error(`server did not listen: ${stdout}`);
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  return child;
}

function incompletePost(port, path, durationMs) {
  return new Promise((resolve) => {
    const started = Date.now();
    let settled = false;
    const finish = (result) => {
      if (settled) return;
      settled = true;
      resolve({ ...result, elapsedMs: Date.now() - started, request });
    };
    const request = http.request({
      host: '127.0.0.1',
      port,
      method: 'POST',
      path,
      headers: { 'Content-Type': 'application/json', 'Content-Length': '64' },
    });
    request.on('response', (response) => {
      response.resume();
      finish({ kind: 'response', status: response.statusCode });
    });
    request.on('error', (error) => finish({ kind: 'error', code: error.code }));
    request.write('{"email":"slow@example.com"');
    setTimeout(() => finish({ kind: 'still-open' }), durationMs);
  });
}

test('an incomplete ordinary request cannot occupy a connection beyond five seconds', async (t) => {
  const port = await reservePort();
  const child = await startServer(port);
  t.after(async () => {
    if (child.exitCode === null) child.kill();
    if (child.exitCode === null) await once(child, 'exit');
  });

  const result = await incompletePost(port, '/auth/login', 5_600);
  result.request.destroy();
  assert.notEqual(result.kind, 'still-open', 'server left a partial request open beyond its 5 s budget');
  assert.ok(result.elapsedMs <= 5_500, `request ended after ${result.elapsedMs} ms`);
});

test('an incomplete reset receives the documented ten-second budget', async (t) => {
  const port = await reservePort();
  const child = await startServer(port);
  t.after(async () => {
    if (child.exitCode === null) child.kill();
    if (child.exitCode === null) await once(child, 'exit');
  });

  const beforeNormalDeadline = await incompletePost(port, '/_test/reset', 5_600);
  beforeNormalDeadline.request.destroy();
  assert.equal(beforeNormalDeadline.kind, 'still-open', 'reset must not inherit the ordinary 5 s request budget');

  const deadlineResult = await incompletePost(port, '/_test/reset', 10_600);
  deadlineResult.request.destroy();
  assert.notEqual(deadlineResult.kind, 'still-open', 'server left a partial reset open beyond its 10 s budget');
  assert.ok(deadlineResult.elapsedMs <= 10_500, `reset ended after ${deadlineResult.elapsedMs} ms`);
});
