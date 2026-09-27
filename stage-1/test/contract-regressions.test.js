'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const handlers = require('../lib/handlers');
const store = require('../lib/store');

const CEILING = 2 ** 53;

function fixture(users, settlementOperatorIds = ['u_op']) {
  return { currency: 'EUR', minor_units: 2, settlement_operator_ids: settlementOperatorIds, users, payments: [], requests: [] };
}

function user(id, handle, balance, password = 'correct horse') {
  return { id, email: `${handle}@example.com`, password, display_name: handle.toUpperCase(), handle, balance };
}

function reset(users, settlementOperatorIds) {
  assert.equal(handlers.testReset({ body: fixture(users, settlementOperatorIds) }).status, 204);
}

function operatorContext(body, key = 'settlement-key') {
  return { user: store.getState().users.get('u_op'), headers: { 'idempotency-key': key }, body };
}

function expectValidation(run) {
  assert.throws(run, (error) => error && error.status === 422 && error.code === 'validation_failed');
}

test('settlement rejects invalid visibility without moving money', () => {
  reset([user('u_op', 'op', 0), user('u_a', 'a', 10), user('u_b', 'b', 0)]);
  expectValidation(() => handlers.createSettlement(operatorContext({ transfers: [{ from_handle: 'a', to_handle: 'b', amount: 1, visibility: 'hidden' }] })));
  assert.equal(store.getState().users.get('u_a').balance, 10);
  assert.equal(store.getState().users.get('u_b').balance, 0);
});

test('settlement rejects a note longer than 200 characters without moving money', () => {
  reset([user('u_op', 'op', 0), user('u_a', 'a', 10), user('u_b', 'b', 0)]);
  expectValidation(() => handlers.createSettlement(operatorContext({ transfers: [{ from_handle: 'a', to_handle: 'b', amount: 1, note: 'x'.repeat(201) }] })));
  assert.equal(store.getState().users.get('u_a').balance, 10);
  assert.equal(store.getState().users.get('u_b').balance, 0);
});

test('import rejects a negative wallet balance and preserves the destination state', () => {
  reset([user('u_op', 'op', 0), user('u_a', 'a', 10), user('u_b', 'b', 0)]);
  const snapshot = JSON.parse(JSON.stringify(handlers.testExport().body));
  snapshot.state.users.find((candidate) => candidate.id === 'u_a').balance = -1;
  expectValidation(() => handlers.testImport({ body: snapshot }));
  assert.equal(store.getState().users.get('u_a').balance, 10);
});

test('reset accepts only the specified minor-unit values and preserves state on rejection', () => {
  reset([user('u_op', 'op', 0), user('u_a', 'a', 10), user('u_b', 'b', 0)]);
  const unsupported = fixture([user('u_op', 'op', 0), user('u_a', 'a', 1), user('u_b', 'b', 9)]);
  unsupported.minor_units = 1;
  expectValidation(() => handlers.testReset({ body: unsupported }));
  assert.equal(store.getState().minorUnits, 2);
  assert.equal(store.getState().users.get('u_a').balance, 10);

  for (const minorUnits of [0, 3]) {
    const allowed = fixture([user('u_op', 'op', 0), user('u_a', 'a', 10), user('u_b', 'b', 0)]);
    allowed.minor_units = minorUnits;
    assert.equal(handlers.testReset({ body: allowed }).status, 204);
    assert.equal(store.getState().minorUnits, minorUnits);
  }
});

test('net-neutral settlement at the balance ceiling preserves every unit', () => {
  reset([user('u_op', 'op', 0), user('u_a', 'a', CEILING), user('u_b', 'b', 1), user('u_c', 'c', 0)]);
  const response = handlers.createSettlement(operatorContext({
    transfers: [{ from_handle: 'b', to_handle: 'a', amount: 1 }, { from_handle: 'a', to_handle: 'c', amount: 1 }],
  }));
  assert.equal(response.status, 201);
  assert.equal(store.getState().users.get('u_a').balance, CEILING);
  assert.equal(store.getState().users.get('u_b').balance, 0);
  assert.equal(store.getState().users.get('u_c').balance, 1);
});
