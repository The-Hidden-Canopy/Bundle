PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS sources (
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

CREATE TABLE IF NOT EXISTS connector_state (
    connector_id TEXT PRIMARY KEY,
    state_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS accounts (
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

CREATE TABLE IF NOT EXISTS balance_observations (
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

CREATE TABLE IF NOT EXISTS transactions (
    account_id TEXT NOT NULL,
    source_transaction_id TEXT NOT NULL,
    current_revision INTEGER NOT NULL CHECK (current_revision > 0),
    PRIMARY KEY (account_id, source_transaction_id),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
);

CREATE TABLE IF NOT EXISTS transaction_revisions (
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

CREATE TABLE IF NOT EXISTS merchants (
    merchant_id TEXT PRIMARY KEY,
    canonical_name TEXT NOT NULL,
    category_hint TEXT,
    confidence TEXT NOT NULL,
    user_confirmed INTEGER NOT NULL CHECK (user_confirmed IN (0, 1))
);

CREATE TABLE IF NOT EXISTS merchant_aliases (
    merchant_id TEXT NOT NULL,
    alias TEXT NOT NULL,
    PRIMARY KEY (merchant_id, alias),
    FOREIGN KEY (merchant_id) REFERENCES merchants(merchant_id)
);

CREATE TABLE IF NOT EXISTS category_assignments (
    transaction_id TEXT NOT NULL,
    category_id TEXT NOT NULL,
    method TEXT NOT NULL,
    confidence TEXT NOT NULL,
    source TEXT NOT NULL,
    revision INTEGER NOT NULL CHECK (revision > 0),
    PRIMARY KEY (transaction_id, revision),
    FOREIGN KEY (transaction_id) REFERENCES transaction_revisions(transaction_id)
);

CREATE TABLE IF NOT EXISTS commitments (
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

CREATE TABLE IF NOT EXISTS commitment_evidence (
    commitment_id TEXT NOT NULL,
    source_id TEXT NOT NULL,
    source_version INTEGER NOT NULL,
    related_transaction_id TEXT,
    PRIMARY KEY (commitment_id, source_id, source_version, related_transaction_id),
    FOREIGN KEY (commitment_id) REFERENCES commitments(commitment_id),
    FOREIGN KEY (source_id, source_version)
        REFERENCES sources(source_id, source_version)
);

CREATE TABLE IF NOT EXISTS income_streams (
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

CREATE TABLE IF NOT EXISTS budgets (
    budget_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    mode TEXT NOT NULL,
    period_rule TEXT NOT NULL,
    rollover_policy TEXT NOT NULL,
    starts_at TEXT NOT NULL,
    ends_at TEXT,
    status TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS budget_allocations (
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

CREATE TABLE IF NOT EXISTS goals (
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

CREATE TABLE IF NOT EXISTS goal_plans (
    goal_id TEXT NOT NULL,
    account_id TEXT NOT NULL,
    PRIMARY KEY (goal_id, account_id),
    FOREIGN KEY (goal_id) REFERENCES goals(goal_id),
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
);

CREATE TABLE IF NOT EXISTS planned_events (
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

CREATE TABLE IF NOT EXISTS forecast_runs (
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

CREATE TABLE IF NOT EXISTS forecast_points (
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

CREATE TABLE IF NOT EXISTS insights (
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

CREATE TABLE IF NOT EXISTS scenarios (
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

CREATE TABLE IF NOT EXISTS import_batches (
    batch_id TEXT PRIMARY KEY,
    connector_id TEXT NOT NULL,
    started_at TEXT NOT NULL,
    completed_at TEXT NOT NULL,
    next_cursor TEXT,
    warnings_json TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS user_overrides (
    override_id TEXT PRIMARY KEY,
    object_type TEXT NOT NULL,
    object_id TEXT NOT NULL,
    field TEXT NOT NULL,
    old_projection_json TEXT NOT NULL,
    new_user_value_json TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS event_log (
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

CREATE TABLE IF NOT EXISTS calculation_receipts (
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

CREATE TRIGGER IF NOT EXISTS sources_append_only_update
BEFORE UPDATE ON sources
BEGIN
    SELECT RAISE(ABORT, 'sources are append-only');
END;

CREATE TRIGGER IF NOT EXISTS sources_append_only_delete
BEFORE DELETE ON sources
BEGIN
    SELECT RAISE(ABORT, 'sources are append-only');
END;

CREATE TRIGGER IF NOT EXISTS sources_validate_content_hash_insert
BEFORE INSERT ON sources
WHEN length(NEW.content_hash) <> 71
  OR substr(NEW.content_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.content_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'sources require a SHA-256 content hash');
END;

CREATE TRIGGER IF NOT EXISTS balance_observations_append_only_update
BEFORE UPDATE ON balance_observations
BEGIN
    SELECT RAISE(ABORT, 'balance observations are append-only');
END;

CREATE TRIGGER IF NOT EXISTS balance_observations_append_only_delete
BEFORE DELETE ON balance_observations
BEGIN
    SELECT RAISE(ABORT, 'balance observations are append-only');
END;

CREATE TRIGGER IF NOT EXISTS transaction_revisions_append_only_update
BEFORE UPDATE ON transaction_revisions
BEGIN
    SELECT RAISE(ABORT, 'transaction revisions are append-only');
END;

CREATE TRIGGER IF NOT EXISTS transaction_revisions_append_only_delete
BEFORE DELETE ON transaction_revisions
BEGIN
    SELECT RAISE(ABORT, 'transaction revisions are append-only');
END;

CREATE TRIGGER IF NOT EXISTS forecast_runs_append_only_update
BEFORE UPDATE ON forecast_runs
BEGIN
    SELECT RAISE(ABORT, 'forecast runs are append-only');
END;

CREATE TRIGGER IF NOT EXISTS forecast_runs_append_only_delete
BEFORE DELETE ON forecast_runs
BEGIN
    SELECT RAISE(ABORT, 'forecast runs are append-only');
END;

CREATE TRIGGER IF NOT EXISTS forecast_runs_validate_input_hash_insert
BEFORE INSERT ON forecast_runs
WHEN length(NEW.input_hash) <> 71
  OR substr(NEW.input_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.input_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'forecast runs require a SHA-256 input hash');
END;

CREATE TRIGGER IF NOT EXISTS forecast_points_append_only_update
BEFORE UPDATE ON forecast_points
BEGIN
    SELECT RAISE(ABORT, 'forecast points are append-only');
END;

CREATE TRIGGER IF NOT EXISTS forecast_points_append_only_delete
BEFORE DELETE ON forecast_points
BEGIN
    SELECT RAISE(ABORT, 'forecast points are append-only');
END;

CREATE TRIGGER IF NOT EXISTS user_overrides_append_only_update
BEFORE UPDATE ON user_overrides
BEGIN
    SELECT RAISE(ABORT, 'user overrides are append-only');
END;

CREATE TRIGGER IF NOT EXISTS user_overrides_append_only_delete
BEFORE DELETE ON user_overrides
BEGIN
    SELECT RAISE(ABORT, 'user overrides are append-only');
END;

CREATE TRIGGER IF NOT EXISTS calculation_receipts_append_only_update
BEFORE UPDATE ON calculation_receipts
BEGIN
    SELECT RAISE(ABORT, 'calculation receipts are append-only');
END;

CREATE TRIGGER IF NOT EXISTS calculation_receipts_append_only_delete
BEFORE DELETE ON calculation_receipts
BEGIN
    SELECT RAISE(ABORT, 'calculation receipts are append-only');
END;

CREATE TRIGGER IF NOT EXISTS calculation_receipts_validate_input_hash_insert
BEFORE INSERT ON calculation_receipts
WHEN length(NEW.input_hash) <> 71
  OR substr(NEW.input_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.input_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'calculation receipts require a SHA-256 input hash');
END;

CREATE TRIGGER IF NOT EXISTS event_log_append_only_update
BEFORE UPDATE ON event_log
BEGIN
    SELECT RAISE(ABORT, 'event log is append-only');
END;

CREATE TRIGGER IF NOT EXISTS event_log_append_only_delete
BEFORE DELETE ON event_log
BEGIN
    SELECT RAISE(ABORT, 'event log is append-only');
END;

CREATE TRIGGER IF NOT EXISTS event_log_validate_content_hash_insert
BEFORE INSERT ON event_log
WHEN length(NEW.content_hash) <> 71
  OR substr(NEW.content_hash, 1, 7) <> 'sha256:'
  OR substr(NEW.content_hash, 8) GLOB '*[^0-9A-Fa-f]*'
BEGIN
    SELECT RAISE(ABORT, 'event log requires a SHA-256 content hash');
END;

CREATE TRIGGER IF NOT EXISTS sources_emit_domain_event
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

CREATE TRIGGER IF NOT EXISTS balance_observations_emit_domain_event
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

CREATE TRIGGER IF NOT EXISTS transaction_revisions_emit_domain_event
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

INSERT OR IGNORE INTO schema_migrations (version, applied_at)
VALUES (1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
