BEGIN TRANSACTION;
CREATE TABLE accounts (account_id TEXT PRIMARY KEY, source_id TEXT NOT NULL, source_version INTEGER NOT NULL,
            display_name TEXT NOT NULL, institution_name TEXT NOT NULL, account_type TEXT NOT NULL,
            account_subtype TEXT NOT NULL, currency TEXT NOT NULL, current_minor INTEGER NOT NULL,
            available_minor INTEGER NOT NULL, balance_observed_at TEXT NOT NULL, balance_freshness TEXT NOT NULL,
            last_four TEXT, status TEXT NOT NULL, include_in_safe_to_spend INTEGER NOT NULL,
            include_in_net_position INTEGER NOT NULL);
INSERT INTO "accounts" VALUES('legacy-checking','legacy-src',1,'Legacy Checking','','checking','','USD',10000,9000,'2025-12-01T00:00:00Z','fresh',NULL,'active',1,1);
CREATE TABLE event_log (event_id TEXT, event_type TEXT, object_id TEXT, occurred_at TEXT, content_hash TEXT);
INSERT INTO "event_log" VALUES('source:legacy-src:1','source.appended','legacy-src','2025-12-01T00:00:00Z','sha256:0000000000000000000000000000000000000000000000000000000000000000');
CREATE TABLE sources (source_id TEXT, source_kind TEXT, provider_id TEXT, external_object_id TEXT,
            imported_at TEXT, observed_at TEXT, content_hash TEXT, source_version INTEGER, metadata_json TEXT);
INSERT INTO "sources" VALUES('legacy-src','csv',NULL,NULL,'2025-12-01T00:00:00Z','2025-12-01T00:00:00Z','sha256:0000000000000000000000000000000000000000000000000000000000000000',1,'{}');
COMMIT;
