'use strict';

const http = require('http');
const fs   = require('fs');
const path = require('path');

const PORT   = parseInt(process.env.PORT, 10) || 3000;
const PUBLIC = path.join(__dirname, 'public');

const MIME = {
  '.js':  'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.ico': 'image/x-icon',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
};

const indexHtml = fs.readFileSync(path.join(PUBLIC, 'index.html'));

const server = http.createServer((req, res) => {
  const pathname = new URL(req.url, 'http://x').pathname;
  const ext      = path.extname(pathname);

  if (ext && ext !== '.html') {
    const file = path.resolve(PUBLIC, pathname.slice(1));
    if (!file.startsWith(PUBLIC)) { res.writeHead(403); res.end(); return; }
    try {
      const content = fs.readFileSync(file);
      res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
      res.end(content);
    } catch {
      res.writeHead(404); res.end('Not found');
    }
  } else {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(indexHtml);
  }
});

server.listen(PORT, '0.0.0.0', () =>
  console.log(`pocketful stage-2 listening on 0.0.0.0:${PORT}`)
);
