'use strict';

const crypto = require('crypto');
const { promisify } = require('util');

const scrypt = promisify(crypto.scrypt);

function createEmptyState() {
  return {
    currency: null,
    minorUnits: null,
    settlementOperatorIds: new Set(),
    users: new Map(),
    usersByHandle: new Map(),
    usersByEmail: new Map(),
    tokens: new Map(),
    payments: new Map(),
    requests: new Map(),
    splits: new Map(),
    settlements: new Map(),
    idempotency: new Map(),
    counters: { user: 0, payment: 0, request: 0, split: 0, settlement: 0 },
  };
}

let state = createEmptyState();

function getState() { return state; }
function replaceState(next) { state = next; }

function nextId(prefix) {
  state.counters[prefix] = (state.counters[prefix] || 0) + 1;
  return `${prefix[0]}_${state.counters[prefix]}_${crypto.randomBytes(4).toString('hex')}`;
}

function newToken() { return crypto.randomBytes(24).toString('hex'); }

// Async scrypt (libuv threadpool) rather than scryptSync: the sync form blocks
// the single event loop for its full cost, which under load starves unrelated
// in-flight requests' body reads past their own request deadline.
async function hashPassword(password) {
  const salt = crypto.randomBytes(16);
  const hash = await scrypt(password, salt, 64);
  return `${salt.toString('hex')}:${hash.toString('hex')}`;
}

async function verifyPassword(password, stored) {
  if (typeof stored !== 'string' || !stored.includes(':')) return false;
  const [saltHex, hashHex] = stored.split(':');
  const salt = Buffer.from(saltHex, 'hex');
  const expected = Buffer.from(hashHex, 'hex');
  const actual = await scrypt(password, salt, expected.length);
  if (actual.length !== expected.length) return false;
  return crypto.timingSafeEqual(actual, expected);
}

function deriveHandleFromEmail(email) {
  const local = email.split('@')[0];
  const replaced = [...local.toLowerCase()].map((ch) => (/[a-z0-9_]/.test(ch) ? ch : '_')).join('');
  return replaced.slice(0, 20);
}

function idempotencyKeyFor(userId, method, path, key) {
  return `${userId}\u0000${method}\u0000${path}\u0000${key}`;
}

module.exports = {
  createEmptyState,
  getState,
  replaceState,
  nextId,
  newToken,
  hashPassword,
  verifyPassword,
  deriveHandleFromEmail,
  idempotencyKeyFor,
};
