# Judged Run Freeze Record

Status: **NOT FROZEN — DISPATCH PROHIBITED**

Recorded: 2026-09-26

This document records only values observed from the selected runtime seats or the current workspace. `UNVERIFIED` is intentional: it is a blocker, not a value to replace by inference.

## Selected seats

| Seat | Judged-run role | Actual declaration status |
| --- | --- | --- |
| Vex | Planner / Architect | Active in the room; local runtime metadata partially observable. |
| Morrow | Builder / Integrator | Declared by the active runtime seat. |
| Flint | Independent Breaker / Test Engineer | Active in the room; the seat explicitly reported every runtime field as `UNVERIFIED`. |
| Wren | Independent Release Verifier | Declared by the active runtime seat. |
| Vale | Browser / Interface Specialist | Declared by the active runtime seat; browser tooling is not provisioned. |

## Source configuration reconciliation

Current source document: `docs/pocketful-riptide-master-engineering-submission-specification.md` (SHA-256 `e60e9cf30561b105844fc5d8d92d131e214caf9e05251faf141534881887d93b`).

Its Sections 5 and 5A select the same five seats listed above. The document is **not itself freeze-ready**: it also retains a seven-seat/Rook/Sable configuration, calls Flint and Wren Feather AI despite Wren's observed opencode declaration, and names `factory/`, `task-packets/`, `submission/`, `human-workload/`, `schemas/`, and `assets/` paths that are absent from this checkout. The five-seat owner selection is recorded; the conflicting source claims remain blockers until the source is corrected or the owner issues an explicit resolution.

## Runtime / model metadata

| Seat | Provider | Exact model | Runtime | Harness | Browser / tooling | Working directory |
| --- | --- | --- | --- | --- | --- | --- |
| Vex | OpenAI | `gpt-5.6-terra` with maximum reasoning effort | Codex agent in the Jam/Band room, profile `default`, session `vex` | `UNVERIFIED` — no mandate declaration has been frozen | N/A at present | `E:\HiddenCanopy\Bundle` |
| Morrow | Anthropic | `claude-sonnet-5` (displayed as `Sonnet 5`) | Claude Agent SDK, orchestrated by Jam/Band | Claude Code | N/A at present | `E:\HiddenCanopy\Bundle` |
| Flint | `UNVERIFIED` | `UNVERIFIED` | `UNVERIFIED` | `UNVERIFIED` | N/A at present | `UNVERIFIED` |
| Wren | opencode | `Kimi K2.7 Code` (owner-reported update; prior declaration was `opencode/big-pickle`) | Band ACP turn, `wren` session, profile `default`, Windows PowerShell 5.1 | opencode with Jam ACP runtime and private Task MCP | N/A at present | `E:\HiddenCanopy\Bundle` |
| Vale | Anthropic | `claude-sonnet-4-6` | Claude Code CLI using Band.ai agent SDK/MCP transport | Band.ai agent platform | `UNVERIFIED` — no browser is available and UI tooling is not provisioned | `D:\THC-code\Bundle\agent` |

## Paths and commands

| Field | Actual value | Status |
| --- | --- | --- |
| Task packet path | `E:\HiddenCanopy\Bundle\pocketful\spec\stage-1.md` is the official Stage 1 source specification. | Source exists; **no frozen dispatch packet has been assembled**. |
| Result-output path | Owner-selected target: `https://github.com/The-Hidden-Canopy/Bundle` at `E:\HiddenCanopy\Bundle`; upstream parent: `https://github.com/band-ai/dark-factory-wearedevs`. Historical reference candidate: `https://github.com/The-Hidden-Canopy/pocketful-results`, detached verification checkout `E:\HiddenCanopy\pocketful-stage1-verify-ddc0ffb`, `stage-1/`, commit `ddc0ffbeb94cbd27acc6d4fdfeae92724b2b2780`. | Bundle is the delivery target by owner direction. The historical candidate has reproducible Stage 1 contract failures and is not eligible for acceptance. |
| Clean build command | `docker build -t pocketful-stage-1 . && docker run --rm -p 8080:8080 -e PORT=8080 pocketful-stage-1` (reported RUN.md command). | **UNVERIFIED**: no Docker build or container receipt exists. |
| Clean start command | `PORT=18080 node server.js` from the detached verification checkout's `stage-1/`. | Independent host-process receipt: service listened and remained responsive. This is not a Docker/clean-container result. |
| Health/readiness check | `GET http://127.0.0.1:18080/health` returned `200 {"status":"ok"}`. | Independent host-only observation; clean-container readiness remains blocked. |
| BAND room export path | `UNVERIFIED` | Export destination has not been selected. |
| Run manifest path | `UNVERIFIED` | `kickoff-manifest.json` is a source-integrity manifest, not a judged-run manifest. |
| Evidence index path | `UNVERIFIED` | No judged candidate or evidence index exists. |

## Freeze checks

| Check | Status | Evidence / blocker |
| --- | --- | --- |
| Five selected mandates hashed | BLOCKED | No mandate files have been created and audited against the selected runtime seats. |
| Task packet hashed | BLOCKED | Official source spec exists, but the frozen run-specific dispatch packet has not been assembled. |
| Model/runtime metadata recorded | BLOCKED | Flint metadata is unverified; Vex exact deployment ID is not exposed. |
| Clean build tested | BLOCKED | Candidate `stage-1/Dockerfile` and RUN.md exist, but Docker is unavailable locally; no clean build receipt exists. |
| Clean start tested | PARTIAL | Detached local Node host process served `/health` and the published Stage 1 harness. Docker clean-container behavior is still unverified. |
| No outbound dependency verified | BLOCKED | Requires a built candidate and isolated run. |
| Result-output path verified | PARTIAL | Owner selected Bundle as the target and its `origin` remote is verified. Bundle lacks an immutable Stage 1 candidate revision and the required factory/eligibility artifacts. |
| Room export location verified | BLOCKED | No export location selected. |
| Run manifest initialized | BLOCKED | No judged-run manifest exists. |
| No task-specific language appears in standing mandates | BLOCKED | No standing mandates exist to audit. |

## Reference candidate received after freeze creation

Morrow reported a Stage 1 implementation at current commit `ddc0ffbeb94cbd27acc6d4fdfeae92724b2b2780`; its parent `ff643e3c1331d6dcf4981f160050092ae7b88de7` is superseded. The current remote branch identity was independently observed, but source content and test results have not yet been independently checked out or reproduced.

The builder reports 65 passing self-test assertions across four scripts, with retained stdout SHA-256 `f207f5a2fb06654db0095207bc0b2466d2c56178f18da881de6bd4781a3516c7`. Exact script paths and the full command transcript have not been placed in this workspace, so that number remains **builder self-test evidence only**.

The superseded parent had a reported exact-`+2^53` precision bug: a transfer of one unit to a recipient already at `9007199254740992` returned success, debited the sender, and silently failed to credit the recipient. The reported repair pre-computes headroom before addition; the same case now rejects with `422 validation_failed` before mutation. This regression account needs independent reproduction against both immutable revisions.

This is useful **reference evidence only**. It was built outside the normal multi-seat judged-run dispatch, has no Docker or isolated-harness receipt, no independent breaker/release verdict, and no completed freeze record. It must not be represented as an accepted or judged submission.

## Independent Vex verification â€” rejection receipt

On 2026-09-26, Vex cloned the public remote into `E:\HiddenCanopy\pocketful-stage1-verify-ddc0ffb`, detached `HEAD` at `ddc0ffbeb94cbd27acc6d4fdfeae92724b2b2780`, and observed an empty `git status --short` plus no `git diff --check` output. The service was started locally from `stage-1/` with Node on ports 18080 and 18081. This is an independent host-process check only; Docker is not installed locally, so no container or isolated-mode claim is made.

The candidate **fails the written Stage 1 contract** before the shipped harness completes:

1. `POST /settlements` with an authenticated operator and `{"transfers":[{"from_handle":"a","to_handle":"b","amount":1,"visibility":"hidden"}]}` must reject invalid visibility under Â§11's ordinary-payment rules. Observed: `201`, a public payment was created, and balances changed from `a:10,b:0` to `a:9,b:1`.
2. The same endpoint accepted a transfer note of 201 characters with `201`, although Â§11 imports the ordinary payment note rule (maximum 200 characters).
3. An export was modified only to make one user's `balance` `-1`, then sent to `POST /_test/import`. Observed: `204`; a subsequent authenticated `GET /me` returned `balance:-1`. Â§10 requires invalid imported state to return `422 validation_failed` without changing destination state, and Â§1 prohibits a negative wallet balance.

Static review identifies the direct paths: `stage-1/lib/handlers.js:createSettlement` coerces non-`private` visibility to `public` and never invokes the ordinary note/visibility validators; `stage-1/lib/serialize.js:deserializeState` validates only a small structural subset and accepts negative balances. These are candidate defects, not merely missing test coverage. No candidate file was edited during this verification.

**Published-suite receipt:** `py -3 -m harness run --track pocketful --base-url http://127.0.0.1:18080 --stages 1 --out E:\HiddenCanopy\Bundle\runs\vex-independent-ddc0ffb-stage1-20260926` completed in host mode with **147/147 published checks passed** in 371.03 seconds. Raw artifacts are `runs/vex-independent-ddc0ffb-stage1-20260926/report.json`, `stage-1.counts.json`, and `stage-1.log`; suite digest `e43e6e926f5990dae5309829a4256e9e0774bc5a9fb56095e66bb110b2ee9bb5`. The run used an external URL, so its report records no Git revision; the separately observed detached checkout SHA above binds the service process to `ddc0ffbeb94cbd27acc6d4fdfeae92724b2b2780`.

**Eligibility receipt:** `py -3 -m harness check E:\HiddenCanopy\pocketful-stage1-verify-ddc0ffb --track pocketful` exited 1. It reports missing `FACTORY.md`, missing `mandates/`, and missing `room.json`. Those are additional judged-run eligibility blockers, independent of the functional defects.

**Disposition:** REJECT this immutable candidate for Stage 1 acceptance. A repair must use a new commit, include regression tests for all three reproductions, and be independently rerun from a fresh checkout. A green published-suite run is retained as diagnostic evidence only; it cannot override the written-contract failures, eligibility failure, or missing Docker/isolated-harness receipt.

## Dispatch decision

Do not dispatch a judged stage. This record must be updated only with observed values and command receipts; it may be frozen only after every check above is PASS and every selected mandate is generic, correctly named, and mapped to its active room seat.

## Local contract repair evidence — not a candidate

After the rejection receipt, a local-only repair worktree was created at `E:\HiddenCanopy\pocketful-stage1-repair-ddc0ffb` on branch `vex/stage1-contract-repair`, based on rejected commit `ddc0ffbeb94cbd27acc6d4fdfeae92724b2b2780`. It is intentionally uncommitted and has not been pushed. It must not be treated as a new candidate until a user-authorized commit and independent fresh-checkout verification exist.

The bounded repair changes only `stage-1/lib/handlers.js`, `stage-1/lib/serialize.js`, `stage-1/package.json`, and new regression tests in `stage-1/test/contract-regressions.test.js`:

1. Settlement member transfers now apply the ordinary-payment note and visibility validators before any mutation.
2. Settlement balances are calculated from validated per-wallet net deltas, then assigned together; member records no longer use a sequential credit/debit path that can silently lose a unit at the exact `2^53` ceiling.
3. Import now rejects users whose serialized balance is non-integral, negative, or over `2^53` before replacing destination state.

**Local targeted test receipt:** `npm test` from the repaired `stage-1/` completed **4/4** contract regressions: invalid settlement visibility and a 201-character settlement note both return `422` without movement; negative-balance import returns `422` while preserving destination state; and a net-neutral two-member settlement at `2^53` retains the exact balances.

**Local HTTP receipt:** a repaired host process on port 18082 returned `422 validation_failed` for invalid visibility, `422 validation_failed` for a 201-character note, and `422 validation_failed` for negative-balance import; balances remained `a:10,b:0` after the first two and `a:10` after failed import. The `b -> a:1`, `a -> c:1` settlement with `a=2^53,b=1,c=0` returned `201` with two payments and final balances `a=2^53,b=0,c=1`.

**Published-suite receipt:** `py -3 -m harness run --track pocketful --base-url http://127.0.0.1:18082 --stages 1 --out E:\HiddenCanopy\Bundle\runs\vex-repair-ddc0ffb-stage1-20260926` completed in host mode with **147/147** checks passed. The report is `runs/vex-repair-ddc0ffb-stage1-20260926/report.json`; it records external-URL provenance and therefore no revision. This does not establish a candidate SHA.

**Remaining blockers:** Docker is unavailable locally, so no clean-container or isolated-mode receipt exists. `py -3 -m harness check E:\HiddenCanopy\pocketful-stage1-repair-ddc0ffb --track pocketful` still reports the same missing `FACTORY.md`, `mandates/`, and `room.json`. No evidence artifact was invented to bypass that gate.

## Bundle target staging evidence — not an accepted candidate

After the owner designated Bundle as the delivery target, the bounded repair was copied into `E:\HiddenCanopy\Bundle\stage-1\`. This is a local, uncommitted staging copy; it has no immutable candidate SHA, has not been pushed, and must not be described as a judged acceptance or release.

**Targeted regression receipt:** `npm test` from `E:\HiddenCanopy\Bundle\stage-1` completed **5/5**: invalid settlement visibility and a 201-character note reject without moving money; negative-balance import rejects while preserving the destination state; a net-neutral two-member settlement at `2^53` retains every unit; and reset accepts only the specified `minor_units` values (`0`, `2`, or `3`) without replacing state on rejection.

**HTTP receipt:** a Bundle host process on port 18083 returned `204` for the fixture reset and `422 validation_failed` for an operator settlement with invalid `visibility:"hidden"`; authenticated balance reads remained `a:10,b:0`. The process was stopped after verification. This is a host-process receipt only.

**Published-suite receipt:** the initial host-mode Bundle run at `runs/vex-bundle-stage1-20260926/report.json` and the post-fixture-repair rerun at `runs/vex-bundle-stage1-minor-units-rerun-20260927/report.json` each completed **147/147** checks passed. Both use suite digest `e43e6e926f5990dae5309829a4256e9e0774bc5a9fb56095e66bb110b2ee9bb5`; external-URL provenance means neither report records a Git revision.

**Eligibility receipt:** `py -3 -m harness check E:\HiddenCanopy\Bundle --track pocketful` reports three failures: missing `FACTORY.md`, missing `mandates/`, and missing required room export `room.json`. Docker is unavailable in the current environment. These are hard evidence and container gates; no placeholder artifact was created to suppress them.

**Timeout review status:** `stage-1/server.js` sets `server.requestTimeout = 0`; the official Stage 1 delivery table specifies a 5-second normal-request budget and 10 seconds for reset. Node's built-in timeout APIs distinguish whole-request parsing timeout from socket inactivity and do not provide a simple route-specific total deadline. No timeout code has been changed on inference alone, because adding a synthetic timeout response would create an unspecified error contract. This remains an explicit source and deployment review item, not a passed conformance claim.

**Independent clean-room content receipt:** Morrow copied the untracked Bundle `stage-1/` source into a newly created directory with no inherited `node_modules`, environment files, or state. In that clean copy, `node --test` completed **4/4**; a standalone process passed health, reset, login, a `201` payment, and a settlement ceiling rejection that left the ceiling balance unchanged. The copied `Dockerfile` was reviewed as a minimal Node 20 Alpine build definition. This supports content portability only. Because `stage-1/` is untracked and absent from Bundle `origin`, it is **not** a fresh-Git-checkout receipt, does not supply an immutable candidate SHA, and does not replace Docker build/run validation.
