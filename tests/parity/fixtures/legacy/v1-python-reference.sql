BEGIN TRANSACTION;
CREATE TABLE accounts (
    account_id TEXT PRIMARY KEY,
    source_id TEXT NOT NULL,
    source_version INTEGER NOT NULL,
    display_name TEXT NOT NULL,
    institution_name TEXT NOT NULL,
    account_type TEXT NOT NULL,
    account_subtype TEXT NOT NULL,
    currency TEXT NOT NULL,
    current_minor INTEGER NOT NULL,
    available_minor INTEGER NOT NULL,
    balance_observed_at TEXT NOT NULL,
    balance_freshness TEXT NOT NULL,
    last_four TEXT,
    status TEXT NOT NULL,
    include_in_safe_to_spend INTEGER NOT NULL CHECK (include_in_safe_to_spend IN (0, 1)),
    include_in_net_position INTEGER NOT NULL CHECK (include_in_net_position IN (0, 1)),
    FOREIGN KEY (source_id, source_version)
        REFERENCES sources(source_id, source_version)
);
INSERT INTO "accounts" VALUES('checking','csv-2026-01',1,'Everyday Checking','','checking','','USD',312345,300000,'2026-01-04T11:00:00Z','fresh',NULL,'active',1,1);
INSERT INTO "accounts" VALUES('savings','csv-2026-01',1,'Rainy Day','','savings','','USD',50000,50000,'2026-01-04T11:00:00Z','fresh',NULL,'active',1,1);
INSERT INTO "accounts" VALUES('card','bank-export-7',2,'Synthetic Card (renamed)','Synthetic Credit Union','credit_card','rewards','USD',0,0,'2026-01-04T11:59:00Z','unavailable','0042','active',1,1);
CREATE TABLE balance_observations (
    observation_id TEXT PRIMARY KEY,
    account_id TEXT NOT NULL,
    current_minor INTEGER NOT NULL,
    available_minor INTEGER NOT NULL,
    observed_at TEXT NOT NULL,
    recorded_at TEXT NOT NULL,
    source_id TEXT NOT NULL,
    source_version INTEGER NOT NULL,
    status TEXT NOT NULL,
    FOREIGN KEY (account_id) REFERENCES accounts(account_id),
    FOREIGN KEY (source_id, source_version)
        REFERENCES sources(source_id, source_version)
);
INSERT INTO "balance_observations" VALUES('bank-export-7:checking:2026-01-04T11:00:00Z','checking',312345,300000,'2026-01-04T11:00:00Z','2026-01-04T12:00:00Z','bank-export-7',1,'valid');
INSERT INTO "balance_observations" VALUES('old-checking','checking',1,1,'2026-01-01T00:00:00Z','2026-01-04T12:00:00Z','bank-export-7',1,'stale');
INSERT INTO "balance_observations" VALUES('bank-export-7:savings:2026-01-04T11:00:00Z','savings',50000,50000,'2026-01-04T11:00:00Z','2026-01-04T12:00:00Z','bank-export-7',1,'valid');
INSERT INTO "balance_observations" VALUES('bank-export-7:card:2026-01-04T11:00:00Z','card',4200,95800,'2026-01-04T11:00:00Z','2026-01-04T12:00:00Z','bank-export-7',1,'valid');
CREATE TABLE budget_allocations (
    budget_id TEXT NOT NULL,
    category_id TEXT NOT NULL,
    limit_minor INTEGER NOT NULL,
    spent_minor INTEGER NOT NULL,
    pending_minor INTEGER NOT NULL,
    remaining_minor INTEGER NOT NULL,
    burn_rate_basis_points INTEGER NOT NULL,
    PRIMARY KEY (budget_id, category_id),
    FOREIGN KEY (budget_id) REFERENCES budgets(budget_id)
);
INSERT INTO "budget_allocations" VALUES('monthly','food',60000,4200,0,55800,700);
CREATE TABLE budgets (
    budget_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    mode TEXT NOT NULL,
    period_rule TEXT NOT NULL,
    rollover_policy TEXT NOT NULL,
    starts_at TEXT NOT NULL,
    ends_at TEXT,
    status TEXT NOT NULL
);
INSERT INTO "budgets" VALUES('monthly','Monthly','category_caps','calendar_month','none','2026-01-01T00:00:00Z',NULL,'active');
CREATE TABLE calculation_receipts (
    receipt_id TEXT PRIMARY KEY,
    engine TEXT NOT NULL,
    engine_version TEXT NOT NULL,
    calculated_at TEXT NOT NULL,
    input_refs_json TEXT NOT NULL,
    input_hash TEXT NOT NULL CHECK (
        length(input_hash) = 71
        AND substr(input_hash, 1, 7) = 'sha256:'
        AND substr(input_hash, 8) NOT GLOB '*[^0-9A-Fa-f]*'
    ),
    policy_id TEXT NOT NULL,
    policy_version TEXT NOT NULL,
    outputs_json TEXT NOT NULL
);
CREATE TABLE category_assignments (
    transaction_id TEXT NOT NULL,
    category_id TEXT NOT NULL,
    method TEXT NOT NULL,
    confidence TEXT NOT NULL,
    source TEXT NOT NULL,
    revision INTEGER NOT NULL CHECK (revision > 0),
    PRIMARY KEY (transaction_id, revision),
    FOREIGN KEY (transaction_id) REFERENCES transaction_revisions(transaction_id)
);
CREATE TABLE commitment_evidence (
    commitment_id TEXT NOT NULL,
    source_id TEXT NOT NULL,
    source_version INTEGER NOT NULL,
    related_transaction_id TEXT,
    PRIMARY KEY (commitment_id, source_id, source_version, related_transaction_id),
    FOREIGN KEY (commitment_id) REFERENCES commitments(commitment_id),
    FOREIGN KEY (source_id, source_version)
        REFERENCES sources(source_id, source_version)
);
INSERT INTO "commitment_evidence" VALUES('rent','csv-2026-01',1,'chk-002');
CREATE TABLE commitments (
    commitment_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    merchant_id TEXT,
    amount_model_json TEXT NOT NULL,
    currency TEXT NOT NULL,
    cadence TEXT NOT NULL,
    next_expected_at TEXT NOT NULL,
    grace_window_millis INTEGER NOT NULL,
    commitment_type TEXT NOT NULL,
    status TEXT NOT NULL,
    confidence TEXT NOT NULL,
    user_confirmed INTEGER NOT NULL CHECK (user_confirmed IN (0, 1)),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    FOREIGN KEY (merchant_id) REFERENCES merchants(merchant_id)
);
INSERT INTO "commitments" VALUES('rent','Rent',NULL,'{"kind":"fixed","primary":{"currency":"USD","minor_units":95000}}','USD','monthly','2026-02-01T00:00:00Z',86400000,'housing','active','supported',1,'2026-01-04T12:00:00Z','2026-01-04T12:00:00Z');
INSERT INTO "commitments" VALUES('utilities','Utilities ⚡',NULL,'{"kind":"range","maximum":{"currency":"USD","minor_units":12000},"minimum":{"currency":"USD","minor_units":8000},"primary":{"currency":"USD","minor_units":10000}}','USD','monthly','2026-01-20T00:00:00Z',0,'utilities','active','inferred',0,'2026-01-04T12:00:00Z','2026-01-04T12:00:00Z');
CREATE TABLE connector_state (
    connector_id TEXT PRIMARY KEY,
    state_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE event_log (
    event_id TEXT PRIMARY KEY,
    event_type TEXT NOT NULL,
    object_id TEXT NOT NULL,
    occurred_at TEXT NOT NULL,
    content_hash TEXT NOT NULL CHECK (
        length(content_hash) = 71
        AND substr(content_hash, 1, 7) = 'sha256:'
        AND substr(content_hash, 8) NOT GLOB '*[^0-9A-Fa-f]*'
    )
);
INSERT INTO "event_log" VALUES('source:csv-2026-01:1','source.appended','csv-2026-01','2026-01-04T12:00:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('account:checking','account.appended','checking','2026-01-01T09:00:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('account:savings','account.appended','savings','2026-01-01T09:00:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('transaction-revision:chk-001','transaction_revision.appended','chk-001','2026-01-01T09:00:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('transaction-revision:chk-002','transaction_revision.appended','chk-002','2026-01-02T15:30:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('transaction-revision:chk-003','transaction_revision.appended','chk-003','2026-01-03T08:15:00.500000Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('transaction-revision:sav-001','transaction_revision.appended','sav-001','2026-01-02T00:00:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('import-batch:csv:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f','import_batch.admitted','csv:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f','2026-01-04T12:00:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f');
INSERT INTO "event_log" VALUES('source:bank-export-7:1','source.appended','bank-export-7','2026-01-04T12:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('account:card','account.appended','card','2026-01-04T11:59:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('balance-observation:bank-export-7:checking:2026-01-04T11:00:00Z','balance_observation.appended','bank-export-7:checking:2026-01-04T11:00:00Z','2026-01-04T12:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('account-balance:checking:bank-export-7:checking:2026-01-04T11:00:00Z','account.balance_projection.updated','checking','2026-01-04T11:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('balance-observation:old-checking','balance_observation.appended','old-checking','2026-01-04T12:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('balance-observation:bank-export-7:savings:2026-01-04T11:00:00Z','balance_observation.appended','bank-export-7:savings:2026-01-04T11:00:00Z','2026-01-04T12:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('account-balance:savings:bank-export-7:savings:2026-01-04T11:00:00Z','account.balance_projection.updated','savings','2026-01-04T11:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('balance-observation:bank-export-7:card:2026-01-04T11:00:00Z','balance_observation.appended','bank-export-7:card:2026-01-04T11:00:00Z','2026-01-04T12:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('transaction-revision:card-001','transaction_revision.appended','card-001','2026-01-04T11:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('import-batch:synthetic-bank:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427','import_batch.admitted','synthetic-bank:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427','2026-01-04T12:00:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427');
INSERT INTO "event_log" VALUES('source:bank-export-7:2','source.appended','bank-export-7','2026-01-05T12:00:00Z','sha256:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3');
INSERT INTO "event_log" VALUES('account-source:card:bank-export-7:2','account.source_projection.updated','card','2026-01-04T11:59:00Z','sha256:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3');
INSERT INTO "event_log" VALUES('transaction-revision:card-001-r2','transaction_revision.appended','card-001-r2','2026-01-04T11:00:00Z','sha256:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3');
INSERT INTO "event_log" VALUES('transaction-current:card:card-001:2','transaction.current_revision.updated','card-001','2026-01-04T11:00:00Z','sha256:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3');
INSERT INTO "event_log" VALUES('import-batch:synthetic-bank:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3','import_batch.admitted','synthetic-bank:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3','2026-01-05T12:00:00Z','sha256:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3');
CREATE TABLE forecast_points (
    run_id TEXT NOT NULL,
    point_at TEXT NOT NULL,
    opening_minor INTEGER NOT NULL,
    inflows_minor INTEGER NOT NULL,
    outflows_minor INTEGER NOT NULL,
    closing_minor INTEGER NOT NULL,
    floor_minor INTEGER NOT NULL,
    currency TEXT NOT NULL,
    confidence TEXT NOT NULL,
    contributing_event_ids_json TEXT NOT NULL,
    PRIMARY KEY (run_id, point_at),
    FOREIGN KEY (run_id) REFERENCES forecast_runs(run_id)
);
CREATE TABLE forecast_runs (
    run_id TEXT PRIMARY KEY,
    calculated_at TEXT NOT NULL,
    start_at TEXT NOT NULL,
    horizon_end TEXT NOT NULL,
    confidence_mode TEXT NOT NULL,
    input_hash TEXT NOT NULL CHECK (
        length(input_hash) = 71
        AND substr(input_hash, 1, 7) = 'sha256:'
        AND substr(input_hash, 8) NOT GLOB '*[^0-9A-Fa-f]*'
    ),
    policy_id TEXT NOT NULL,
    policy_version TEXT NOT NULL
);
CREATE TABLE goal_plans (
    goal_id TEXT NOT NULL,
    account_id TEXT NOT NULL,
    PRIMARY KEY (goal_id, account_id),
    FOREIGN KEY (goal_id) REFERENCES goals(goal_id),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
);
INSERT INTO "goal_plans" VALUES('buffer','checking');
CREATE TABLE goals (
    goal_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    target_minor INTEGER NOT NULL CHECK (target_minor >= 0),
    currency TEXT NOT NULL,
    target_date TEXT,
    current_reserved_minor INTEGER NOT NULL CHECK (current_reserved_minor >= 0),
    planned_contribution_minor INTEGER NOT NULL CHECK (planned_contribution_minor >= 0),
    priority INTEGER NOT NULL,
    protected INTEGER NOT NULL CHECK (protected IN (0, 1)),
    status TEXT NOT NULL
);
INSERT INTO "goals" VALUES('buffer','Emergency buffer',200000,'USD','2026-02-15T00:00:00Z',50000,25000,1,1,'active');
CREATE TABLE import_batches (
    batch_id TEXT PRIMARY KEY,
    connector_id TEXT NOT NULL,
    started_at TEXT NOT NULL,
    completed_at TEXT NOT NULL,
    next_cursor TEXT,
    warnings_json TEXT NOT NULL DEFAULT '[]'
);
INSERT INTO "import_batches" VALUES('csv:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f','csv','2026-01-04T12:00:00Z','2026-01-04T12:00:00Z',NULL,'[]');
INSERT INTO "import_batches" VALUES('synthetic-bank:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427','synthetic-bank','2026-01-04T11:59:00Z','2026-01-04T12:00:00Z','cursor-after-7','["synthetic warning \u2014 none real"]');
INSERT INTO "import_batches" VALUES('synthetic-bank:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3','synthetic-bank','2026-01-05T12:00:00Z','2026-01-05T12:00:00Z',NULL,'[]');
CREATE TABLE income_streams (
    income_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    account_id TEXT NOT NULL,
    payer_merchant_id TEXT,
    amount_model_json TEXT NOT NULL,
    cadence TEXT NOT NULL,
    next_expected_at TEXT NOT NULL,
    confidence TEXT NOT NULL,
    include_in_forecast INTEGER NOT NULL CHECK (include_in_forecast IN (0, 1)),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id),
    FOREIGN KEY (payer_merchant_id) REFERENCES merchants(merchant_id)
);
CREATE TABLE insights (
    insight_id TEXT PRIMARY KEY,
    reason_code TEXT NOT NULL,
    severity TEXT NOT NULL,
    title TEXT NOT NULL,
    explanation TEXT NOT NULL,
    effective_at TEXT NOT NULL,
    expires_at TEXT,
    amount_at_risk_minor INTEGER NOT NULL,
    currency TEXT NOT NULL,
    evidence_json TEXT NOT NULL
);
CREATE TABLE merchant_aliases (
    merchant_id TEXT NOT NULL,
    alias TEXT NOT NULL,
    PRIMARY KEY (merchant_id, alias),
    FOREIGN KEY (merchant_id) REFERENCES merchants(merchant_id)
);
CREATE TABLE merchants (
    merchant_id TEXT PRIMARY KEY,
    canonical_name TEXT NOT NULL,
    category_hint TEXT,
    confidence TEXT NOT NULL,
    user_confirmed INTEGER NOT NULL CHECK (user_confirmed IN (0, 1))
);
CREATE TABLE planned_events (
    event_id TEXT PRIMARY KEY,
    kind TEXT NOT NULL,
    expected_at TEXT NOT NULL,
    amount_model_json TEXT NOT NULL,
    direction TEXT NOT NULL,
    confidence TEXT NOT NULL,
    account_scope TEXT NOT NULL,
    source_type TEXT NOT NULL,
    source_id TEXT,
    source_version INTEGER,
    FOREIGN KEY (source_id, source_version)
        REFERENCES sources(source_id, source_version)
);
CREATE TABLE scenarios (
    scenario_id TEXT PRIMARY KEY,
    amount_minor INTEGER NOT NULL CHECK (amount_minor >= 0),
    currency TEXT NOT NULL,
    scenario_at TEXT NOT NULL,
    account_id TEXT NOT NULL,
    category_id TEXT,
    description TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
);
CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
);
INSERT INTO "schema_migrations" VALUES(1,'2026-10-29T15:54:29.762Z');
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE sources (
    source_id TEXT NOT NULL,
    source_kind TEXT NOT NULL,
    provider_id TEXT,
    external_object_id TEXT,
    imported_at TEXT NOT NULL,
    observed_at TEXT NOT NULL,
    content_hash TEXT NOT NULL CHECK (
        length(content_hash) = 71
        AND substr(content_hash, 1, 7) = 'sha256:'
        AND substr(content_hash, 8) NOT GLOB '*[^0-9A-Fa-f]*'
    ),
    source_version INTEGER NOT NULL CHECK (source_version > 0),
    metadata_json TEXT NOT NULL DEFAULT '{}',
    PRIMARY KEY (source_id, source_version)
);
INSERT INTO "sources" VALUES('csv-2026-01','csv',NULL,'chk-001','2026-01-04T12:00:00Z','2026-01-01T09:00:00Z','sha256:15f8626fe8f0b8fbe304e63a12d4a47809cfa68b8b5141aff41b65451641718f',1,'{}');
INSERT INTO "sources" VALUES('bank-export-7','local_api',NULL,'export-7','2026-01-04T12:00:00Z','2026-01-04T11:59:00Z','sha256:2682bb2ea8d5a94011ddf908c7f0141eec54b2ab96e2270287d7fd9a13549427',1,'{"exporter":"synthetic","note":"na\u00efve caf\u00e9 \u2615","pages":2,"ratio":0.1}');
INSERT INTO "sources" VALUES('bank-export-7','local_api',NULL,'export-7','2026-01-05T12:00:00Z','2026-01-04T11:59:00Z','sha256:476ab5336db913185f2f34ccfec4bf94822214f77d036e8bc2f7871ec112aaa3',2,'{"exporter":"synthetic","note":"na\u00efve caf\u00e9 \u2615","pages":2,"ratio":0.1}');
CREATE TABLE transaction_revisions (
    transaction_id TEXT PRIMARY KEY,
    account_id TEXT NOT NULL,
    source_transaction_id TEXT NOT NULL,
    revision INTEGER NOT NULL CHECK (revision > 0),
    amount_minor INTEGER NOT NULL CHECK (amount_minor >= 0),
    currency TEXT NOT NULL,
    direction TEXT NOT NULL,
    transaction_state TEXT NOT NULL,
    merchant_raw TEXT NOT NULL,
    merchant_id TEXT,
    description TEXT NOT NULL,
    category_id TEXT,
    authorized_at TEXT,
    posted_at TEXT,
    observed_at TEXT NOT NULL,
    recurring_candidate_id TEXT,
    source_id TEXT NOT NULL,
    source_version INTEGER NOT NULL,
    supersedes_transaction_revision INTEGER,
    user_note TEXT NOT NULL DEFAULT '',
    user_tags_json TEXT NOT NULL DEFAULT '[]',
    UNIQUE (account_id, source_transaction_id, revision),
    FOREIGN KEY (account_id, source_transaction_id)
        REFERENCES transactions(account_id, source_transaction_id),
    FOREIGN KEY (source_id, source_version)
        REFERENCES sources(source_id, source_version)
);
INSERT INTO "transaction_revisions" VALUES('chk-001','checking','chk-001',1,125000,'USD','credit','posted','SYNTHETIC EMPLOYER',NULL,'Payroll deposit',NULL,NULL,NULL,'2026-01-01T09:00:00Z',NULL,'csv-2026-01',1,NULL,'','[]');
INSERT INTO "transaction_revisions" VALUES('chk-002','checking','chk-002',1,95000,'USD','debit','posted','SYNTHETIC LANDLORD',NULL,'Rent, January',NULL,NULL,NULL,'2026-01-02T15:30:00Z',NULL,'csv-2026-01',1,NULL,'','[]');
INSERT INTO "transaction_revisions" VALUES('chk-003','checking','chk-003',1,1250,'USD','debit','pending','CAFE SYNTH',NULL,'Café ☕ "oat" latte
second line',NULL,NULL,NULL,'2026-01-03T08:15:00.500000Z',NULL,'csv-2026-01',1,NULL,'','[]');
INSERT INTO "transaction_revisions" VALUES('sav-001','savings','sav-001',1,50000,'USD','credit','posted','',NULL,'Transfer in',NULL,NULL,NULL,'2026-01-02T00:00:00Z',NULL,'csv-2026-01',1,NULL,'','[]');
INSERT INTO "transaction_revisions" VALUES('card-001','card','card-001',1,4200,'USD','debit','posted','SYNTH GROCER #12',NULL,'Groceries','food','2026-01-03T17:00:00Z','2026-01-04T00:00:00Z','2026-01-04T11:00:00Z',NULL,'bank-export-7',1,NULL,'weekly shop','["groceries","w\u00f6chentlich"]');
INSERT INTO "transaction_revisions" VALUES('card-001-r2','card','card-001',2,4150,'USD','debit','posted','SYNTH GROCER #12',NULL,'Groceries (adjusted)','food','2026-01-03T17:00:00Z','2026-01-04T00:00:00Z','2026-01-04T11:00:00Z',NULL,'bank-export-7',2,1,'weekly shop','["groceries","w\u00f6chentlich"]');
CREATE TABLE transactions (
    account_id TEXT NOT NULL,
    source_transaction_id TEXT NOT NULL,
    current_revision INTEGER NOT NULL CHECK (current_revision > 0),
    PRIMARY KEY (account_id, source_transaction_id),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
);
INSERT INTO "transactions" VALUES('checking','chk-001',1);
INSERT INTO "transactions" VALUES('checking','chk-002',1);
INSERT INTO "transactions" VALUES('checking','chk-003',1);
INSERT INTO "transactions" VALUES('savings','sav-001',1);
INSERT INTO "transactions" VALUES('card','card-001',2);
CREATE TABLE user_overrides (
    override_id TEXT PRIMARY KEY,
    object_type TEXT NOT NULL,
    object_id TEXT NOT NULL,
    field TEXT NOT NULL,
    old_projection_json TEXT NOT NULL,
    new_user_value_json TEXT NOT NULL,
    created_at TEXT NOT NULL
);
INSERT INTO "user_overrides" VALUES('override-1','transaction','chk-003','category_id','null','"coffee"','2026-01-04T12:00:00Z');
CREATE TRIGGER sources_append_only_update
BEFORE UPDATE ON sources
BEGIN
    SELECT RAISE(ABORT, 'sources are append-only');
END;
CREATE TRIGGER sources_append_only_delete
BEFORE DELETE ON sources
BEGIN
    SELECT RAISE(ABORT, 'sources are append-only');
END;
CREATE TRIGGER sources_validate_content_hash_insert
BEFORE INSERT ON sources
WHEN length(NEW.content_hash) <> 71
  OR substr(NEW.content_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.content_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'sources require a SHA-256 content hash');
END;
CREATE TRIGGER balance_observations_append_only_update
BEFORE UPDATE ON balance_observations
BEGIN
    SELECT RAISE(ABORT, 'balance observations are append-only');
END;
CREATE TRIGGER balance_observations_append_only_delete
BEFORE DELETE ON balance_observations
BEGIN
    SELECT RAISE(ABORT, 'balance observations are append-only');
END;
CREATE TRIGGER transaction_revisions_append_only_update
BEFORE UPDATE ON transaction_revisions
BEGIN
    SELECT RAISE(ABORT, 'transaction revisions are append-only');
END;
CREATE TRIGGER transaction_revisions_append_only_delete
BEFORE DELETE ON transaction_revisions
BEGIN
    SELECT RAISE(ABORT, 'transaction revisions are append-only');
END;
CREATE TRIGGER forecast_runs_append_only_update
BEFORE UPDATE ON forecast_runs
BEGIN
    SELECT RAISE(ABORT, 'forecast runs are append-only');
END;
CREATE TRIGGER forecast_runs_append_only_delete
BEFORE DELETE ON forecast_runs
BEGIN
    SELECT RAISE(ABORT, 'forecast runs are append-only');
END;
CREATE TRIGGER forecast_runs_validate_input_hash_insert
BEFORE INSERT ON forecast_runs
WHEN length(NEW.input_hash) <> 71
  OR substr(NEW.input_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.input_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'forecast runs require a SHA-256 input hash');
END;
CREATE TRIGGER forecast_points_append_only_update
BEFORE UPDATE ON forecast_points
BEGIN
    SELECT RAISE(ABORT, 'forecast points are append-only');
END;
CREATE TRIGGER forecast_points_append_only_delete
BEFORE DELETE ON forecast_points
BEGIN
    SELECT RAISE(ABORT, 'forecast points are append-only');
END;
CREATE TRIGGER user_overrides_append_only_update
BEFORE UPDATE ON user_overrides
BEGIN
    SELECT RAISE(ABORT, 'user overrides are append-only');
END;
CREATE TRIGGER user_overrides_append_only_delete
BEFORE DELETE ON user_overrides
BEGIN
    SELECT RAISE(ABORT, 'user overrides are append-only');
END;
CREATE TRIGGER calculation_receipts_append_only_update
BEFORE UPDATE ON calculation_receipts
BEGIN
    SELECT RAISE(ABORT, 'calculation receipts are append-only');
END;
CREATE TRIGGER calculation_receipts_append_only_delete
BEFORE DELETE ON calculation_receipts
BEGIN
    SELECT RAISE(ABORT, 'calculation receipts are append-only');
END;
CREATE TRIGGER calculation_receipts_validate_input_hash_insert
BEFORE INSERT ON calculation_receipts
WHEN length(NEW.input_hash) <> 71
  OR substr(NEW.input_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.input_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'calculation receipts require a SHA-256 input hash');
END;
CREATE TRIGGER event_log_append_only_update
BEFORE UPDATE ON event_log
BEGIN
    SELECT RAISE(ABORT, 'event log is append-only');
END;
CREATE TRIGGER event_log_append_only_delete
BEFORE DELETE ON event_log
BEGIN
    SELECT RAISE(ABORT, 'event log is append-only');
END;
CREATE TRIGGER event_log_validate_content_hash_insert
BEFORE INSERT ON event_log
WHEN length(NEW.content_hash) <> 71
  OR substr(NEW.content_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.content_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'event log requires a SHA-256 content hash');
END;
CREATE TRIGGER sources_emit_domain_event
AFTER INSERT ON sources
BEGIN
    INSERT INTO event_log (
        event_id,
        event_type,
        object_id,
        occurred_at,
        content_hash
    ) VALUES (
        'source:' || NEW.source_id || ':' || NEW.source_version,
        'source.appended',
        NEW.source_id,
        NEW.imported_at,
        NEW.content_hash
    );
END;
CREATE TRIGGER balance_observations_emit_domain_event
AFTER INSERT ON balance_observations
BEGIN
    INSERT INTO event_log (
        event_id,
        event_type,
        object_id,
        occurred_at,
        content_hash
    )
    SELECT
        'balance-observation:' || NEW.observation_id,
        'balance_observation.appended',
        NEW.observation_id,
        NEW.recorded_at,
        sources.content_hash
    FROM sources
    WHERE sources.source_id = NEW.source_id
      AND sources.source_version = NEW.source_version;
END;
CREATE TRIGGER transaction_revisions_emit_domain_event
AFTER INSERT ON transaction_revisions
BEGIN
    INSERT INTO event_log (
        event_id,
        event_type,
        object_id,
        occurred_at,
        content_hash
    )
    SELECT
        'transaction-revision:' || NEW.transaction_id,
        'transaction_revision.appended',
        NEW.transaction_id,
        NEW.observed_at,
        sources.content_hash
    FROM sources
    WHERE sources.source_id = NEW.source_id
      AND sources.source_version = NEW.source_version;
END;
COMMIT;
