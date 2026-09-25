"""Stage 4 sample — the shipped smoke checks.

This is a sample, not the graded suite. It shows the shape of each surface once so a
service can be wired up against it; the spec is the contract and the graded checks are
the spec's. Passing everything here means the wiring is right, not that the stage holds.
"""
from types import SimpleNamespace

import pytest

import fixtures as fx
from harness.http import assert_status, new_key

pytestmark = pytest.mark.stage(4)

TTL = 3600


def fixture(users=None, ttl=TTL, authorizations=None):
    base = fx.fixture(users=users)
    base["authorization_ttl_seconds"] = ttl
    base["authorizations"] = authorizations or []
    return base


@pytest.fixture
def world(reset, api):
    seeded = fixture()
    reset(seeded)
    return SimpleNamespace(
        fixture=seeded, total=fx.seeded_total(seeded),
        ada=api().authenticate(fx.ADA["email"], fx.ADA["password"]),
        bob=api().authenticate(fx.BOB["email"], fx.BOB["password"]),
        cy=api().authenticate(fx.CY["email"], fx.CY["password"]))


def wallet(client) -> dict:
    return assert_status(client.get("/me"), 200).json()


def authorize(client, *, to_handle="bob", amount=2000, key=None, **extra):
    body = {"to_handle": to_handle, "amount": amount}
    body.update(extra)
    return client.post("/authorizations", json=body,
                       idempotency_key=new_key() if key is None else key)


def test_me_keeps_balance_and_gains_available_and_held(world):
    me = wallet(world.ada)
    assert me["balance"] == fx.ADA["balance"]
    assert me["balance"] == me["total"], "balance equals total"
    assert me["available"] == me["total"] and me["held"] == 0, \
        "with no open holds all three agree, so the earlier suites behave identically"


def test_a_hold_moves_nothing_but_reserves(world):
    before = wallet(world.bob)["total"]
    assert_status(authorize(world.ada, amount=2000), 201)
    me = wallet(world.ada)
    assert me["total"] == fx.ADA["balance"], "a hold moves no money"
    assert me["held"] == 2000
    assert me["available"] == fx.ADA["balance"] - 2000
    assert wallet(world.bob)["total"] == before, "the receiver gains nothing yet"


def test_authorization_shape(world):
    body = assert_status(authorize(world.ada, amount=2000, note="deposit",
                                   visibility="private"), 201).json()
    assert body["from_handle"] == "ada" and body["to_handle"] == "bob"
    assert body["amount"] == 2000 and body["captured_amount"] == 0
    assert body["status"] == "open" and body["payment_id"] is None
    assert body["note"] == "deposit" and body["visibility"] == "private"
    assert body["expires_at"] and body["created_at"] and body["authorization_id"]


def test_full_capture_moves_the_money_once(world):
    aid = authorize(world.ada, amount=2000).json()["authorization_id"]
    payment = assert_status(world.bob.post(f"/authorizations/{aid}/capture", json={},
                                           idempotency_key=new_key()), 201).json()
    assert payment["amount"] == 2000
    assert payment["authorization_id"] == aid and payment["request_id"] is None
    ada, bob = wallet(world.ada), wallet(world.bob)
    assert ada["total"] == fx.ADA["balance"] - 2000 and ada["held"] == 0
    assert bob["total"] == fx.BOB["balance"] + 2000
    assert ada["total"] + bob["total"] + wallet(world.cy)["total"] == world.total


def test_void_releases_the_hold(world):
    aid = authorize(world.ada, amount=2000).json()["authorization_id"]
    body = assert_status(world.ada.post(f"/authorizations/{aid}/void"), 200).json()
    assert body["status"] == "voided"
    me = wallet(world.ada)
    assert me["held"] == 0 and me["available"] == me["total"] == fx.ADA["balance"]


def test_list_is_scoped_and_filtered(world):
    aid = authorize(world.ada, amount=2000).json()["authorization_id"]
    assert world.cy.get("/authorizations").json()["authorizations"] == []
    outgoing = world.ada.get("/authorizations", params={"direction": "outgoing"}).json()
    assert [a["authorization_id"] for a in outgoing["authorizations"]] == [aid]
    incoming = world.bob.get("/authorizations", params={"direction": "incoming"}).json()
    assert [a["authorization_id"] for a in incoming["authorizations"]] == [aid]
    assert world.ada.get("/authorizations",
                         params={"direction": "incoming"}).json()["authorizations"] == []


def test_operator_can_correct_a_payment_in_a_batch(reset, api):
    import fixtures as fx
    from harness.http import new_key
    fixture = fx.fixture()
    fixture['settlement_operator_ids'] = ['u_ada']
    reset(fixture)
    ada = api().authenticate(fx.ADA['email'], fx.ADA['password'])
    payment = ada.post('/payments', json=dict(to_handle='bob', amount=100), idempotency_key=new_key()).json()
    response = ada.post('/correction-batches', json={'corrections': [dict(
        payment_id=payment['payment_id'], expected_revision=1, amount=50,
        effective_at=payment['created_at'], reason='correction')]}, idempotency_key=new_key())
    assert response.status_code == 201
    assert ada.get('/me').json()['balance'] == 9950


def test_a_refund_is_a_linked_reverse_payment(world):
    payment = assert_status(world.ada.post('/payments', json={'to_handle': 'bob', 'amount': 1000},
                                           idempotency_key=new_key()), 201).json()
    refund = assert_status(world.bob.post('/payments/' + payment['payment_id'] + '/refunds',
                                          json={'amount': 200}, idempotency_key=new_key()), 201).json()
    assert refund['refund_of'] == payment['payment_id']
    assert wallet(world.ada)['total'] == fx.ADA['balance'] - 800
