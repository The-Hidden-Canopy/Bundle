# Pocketful + Riptide Factory Delivery Plan

## Status and planning basis

**Status:** initial execution map; no implementation or submission repository has been created by this plan.

This plan is derived from the current authoritative room plan and the supplied Dark Factory kickoff package. The current checkout (`E:\HiddenCanopy\Bundle`) is the **kickoff package**, not the repository that may be submitted. Its Git worktree is clean at `main...origin/main`.

The governing order for this run is:

1. Official Pocketful stage specifications and organizer corrections.
2. Participant-guide eligibility, gates, and submission rules.
3. `docs/pocketful-riptide-master-engineering-submission-specification.md` (SHA-256 `e60e9cf30561b105844fc5d8d92d131e214caf9e05251faf141534881887d93b`) for the owner-selected five-seat factory and internal guidance, only where it does not conflict with items 1–2 or itself.
4. The earlier Bundle PDF as background design guidance, only where it agrees with items 1–3.
5. This delivery plan and the eventual run-specific task packet.

Optional Riptide behavior never overrides a Pocketful invariant, a stage requirement, a clean-container constraint, or submission eligibility.

## Confirmed constraints

- A qualifying entry requires a separate public result repository, a complete Stage 1 service, at least three distinct configured coding seats, generic mandate files, and a full room export.
- The judged run must be a fresh room and fresh result repository. After dispatching a stage, its seats must resolve the supplied specification internally; human steering, approval, or reruns invalidate the autonomy evidence.
- Each `stage-N/` is a complete independent service for exactly that stage's accumulated requirements. It must not be a pointer to a later implementation, include a nested `.git`, or accidentally satisfy every next-stage test.
- Runtime execution has no outbound network. A stage must build and start from its own Dockerfile and RUN.md.
- Pocketful money is integer minor units, preserves seeded total value, never permits negative balances, processes idempotent writes once, and makes multi-item financial changes atomically under concurrency.
- Riptide is local and deterministic. Every intervention must be traceable to actual local context, reason code, severity, visual state, and the user's choice. Missing or stale context resolves to neutral/uncertain rather than invented reassurance.

## Current blockers to an actual judged run

| Blocker | Why it matters | Owner to resolve | Evidence that closes it |
| --- | --- | --- | --- |
| No separate result checkout/path has been supplied | The kickoff package must not become the submitted implementation repository. | Human owner | Absolute path and intended public remote for the result repository. |
| Judged-run roster is provisionally selected but not frozen | The owner selected Vex, Morrow, Flint, Wren, and Vale, recorded in `judged-run-freeze.md`. It cannot freeze until every runtime declaration is observed, mandate files are generic and correctly named, and the selected working paths are reconciled. | Human owner with Vex | Complete runtime metadata; active runtime proof; reciprocal room handoff receipt; and one generic, correctly named mandate per selected seat. |
| Live five-seat source document has unresolved internal contradictions | It declares the five-seat Vex/Morrow/Flint/Wren/Vale configuration but retains seven-seat/Rook/Sable routing, inaccurate Feather runtime assertions, and references to factory/submission paths absent from this checkout. | Human owner with Vex | A corrected source revision or an explicit owner ruling on the authoritative five-seat sections; actual runtime declarations; and created/verified artifact paths. |
| Harness/model declarations and generic mandate files are not yet frozen for the judged run | Gate 1 requires one correctly named mandate per seat with actual harness and exact model; product terms in a mandate are disqualifying. | Vex / seat owners | Mandate audit, hashes, and a roster-to-filename mapping. |
| Clean-container and browser-check readiness has not been established | A green local process is not equivalent to the required Docker and Playwright evidence. | Selected release-verifier seat | Docker daemon/readiness receipt and a reproducible harness environment record. |

These are planning gates, not permission to fabricate evidence or begin a judged dark-factory run prematurely.

## Factory-to-product separation

| Surface | Contains | Must not contain |
| --- | --- | --- |
| `FACTORY.md` and `mandates/` | Generic seat ownership, handoff format, evidence/rejection rules, harness/model facts, measured factory costs. | Pocketful, Riptide, endpoint names, fields, error codes, stage fixtures, or product-specific test logic. |
| Room task packet | Complete official stage requirements, acceptance criteria, local asset inputs, product invariants, and known ambiguity decisions. | New standing mandates or unrecorded human steering after dispatch. |
| `stage-N/` | The buildable service, Dockerfile, RUN.md, locally bundled assets, and its tests/source. | Later-stage behavior or external runtime dependencies. |
| Evidence and room export | Full handoffs, committed revisions, test receipts, rejection/fix loop, intervention log, and final verdict. | Credentials, fabricated results, or altered room-history claims. |

## Delivery sequence

### Phase 0 — Factory qualification and run boundary

**Objective:** create the conditions for a valid autonomous run before any judged product code is dispatched.

1. Select and initialize the separate result repository; record its absolute path and intended public remote.
2. Confirm and configure the provisional Vex/Morrow/Flint/Wren/Vale seat map; use the PDF's four-role model (planner, builder, breaker, release verifier) plus the browser specialist without blurring independent release authority. Do not substitute inactive or absent seats in the evidence trail.
3. Freeze generic mandates, record each mandate hash, actual harness, exact model identifier, and seat-name mapping.
4. Create the result-repository skeleton: `README.md`, `FACTORY.md`, `mandates/`, empty future stage folders only as they become complete, and submission/evidence locations.
5. Record Docker, Python/harness, Playwright, Git identity, and room-recording readiness without committing credentials.
6. Define the complete Stage 1 dispatch packet from the official spec. Resolve material ambiguities before dispatch; after dispatch, record blockers rather than seeking human steering.

**Gate P0:** Vex may dispatch the first judged stage only when every blocker above has closing evidence and the task packet is complete.

### Phase 1 — Stage 1: financial core and HTTP contract

**Objective:** deliver a clean-container JSON API whose financial state remains correct under retries, concurrency, malformed input, and restart/import-export boundaries.

| Workstream | Primary seat | Required outcome |
| --- | --- | --- |
| Requirement and acceptance matrix | Selected planner / architect seat | Requirement-to-evidence matrix covering HTTP rules, authorization, error precedence, exact arithmetic, privacy, idempotency, import/export, and all atomicity invariants. |
| Service and persistence | Selected builder / integrator seat | Authoritative local state model, authentication, request validation, exact minor-unit arithmetic, transactional/idempotent writes, activity/request/split/settlement behavior, and export/import. |
| Contract/integration design | Selected builder with planner handoff | Agreed API and persistence boundaries before parallel edits; one integrated candidate that builds and starts by the documented command. |
| Adversarial validation | Selected breaker / test-engineer seat | Independent tests for duplicate keys, body mismatch, insufficient funds, parallel spends, split remainder allocation, settlement all-or-nothing behavior, hidden/private feed visibility, reset atomicity, malformed JSON, and import/restart replay. |
| Release verdict | Selected release-verifier seat that did not author the candidate | `ACCEPT`, `REJECT`, or `INSUFFICIENT_EVIDENCE` based on requirements, code identity, command receipts, failures, and clean-container result. |

**Stage 1 exit evidence:** independent clean build/start, shipped harness result, spec-derived negative tests beyond shipped checks, concurrency double-spend receipt, idempotency/retry receipt, total-conservation/reconciliation receipt, and `ACCEPT` from the selected non-author release verifier.

**Snapshot action:** only after `ACCEPT`, materialize the accepted candidate into `stage-1/`, ensure no nested Git repository, hash it, and run the stage check against a fresh clone.

### Phase 2 — Stage 2: browser product, holds, and stale-state recovery

**Objective:** extend the accepted Stage 1 snapshot—without mutating its submission copy—with the required HTML/browser experience, authorizations/partial captures, and recovery from stale or lost responses.

| Workstream | Primary seat | Required outcome |
| --- | --- | --- |
| API extension and held-funds model | Selected builder / integrator seat | Available/held/total correctness; authorization lifecycle, expiry, partial/final capture, void, permissions, idempotency, and serializable concurrent operations. |
| Browser interaction | Selected browser-capable seat | Required routes, `data-testid` contracts, accessible loading/error/empty states, accurate display of seeded and live financial state, and lost-response recovery. |
| Integrated candidate | Selected builder / integrator seat | Single self-contained Stage 2 service that retains all Stage 1 semantics and passes API plus browser checks. |
| Optional Riptide slice | Selected product implementation seat, only after the required stage behavior is proven | Offline deterministic resolver whose output cannot change the financial result; neutral/uncertain result for stale or missing context. It must remain inside the stage boundary and cannot be a reason to add speculative APIs or later-stage behavior. |

**Stage 2 adversarial focus:** stale UI after reset, duplicate/in-flight submission, lost-success response, available-versus-total confusion, unauthorized capture/void, expiry boundaries, partial capture remainder, explicit-offset timestamp handling, and a Riptide prompt whose displayed reason cannot be traced to local state.

**Gate P2:** snapshot only after the selected breaker's independent attack set and the selected non-author release verifier's `ACCEPT`; run Stage 1 and Stage 2 suites against the snapshot and verify it does not claim Stage 3.

### Phase 3 — Stage 3: truthful temporal history

**Objective:** preserve original receipts while adding immutable correction revisions, effective time versus recorded time, historical balances/holds, and snapshot-stable statement paging.

1. Define a time model with explicit-offset RFC 3339 parsing only; naive timestamps and unbounded/future-invalid write times reject deterministically.
2. Model immutable payment revisions and correction idempotency so a retry returns the original response and a competing correction cannot both win.
3. Validate historical total and available balances at each effective/event boundary before committing a correction; preserve prior history and idempotency state on every rejection.
4. Freeze statement pages with caller-bound, reset-scoped snapshots. A snapshot must not change as later writes or corrections occur.
5. Import prior-stage exports without losing authorizations, captures, receipts, privacy, or stage-appropriate semantics.

**Stage 3 adversarial focus:** cross-user revision access, stale expected revision races, naive-offset variants, correction-induced historical overdraft, snapshot theft/reset leakage, backdated corrections, historical hold expiry, paginated statement drift, and seeded future timestamps.

**Gate P3:** the selected non-author release verifier accepts after a fresh candidate passes Stage 1–3 requirements and an independent export/import plus concurrent-correction receipt exists. Snapshot into `stage-3/` only then.

### Phase 4 — Stage 4: refunds and atomic correction batches

**Objective:** add refunds and settlement-operator batch corrections without losing any earlier receipt, statement snapshot, available-funds rule, or atomicity guarantee.

1. Implement receiver-only refunds from available funds; prohibit refund-of-refund and cumulative excess refund behavior.
2. Implement a batch correction transaction with deterministic input-order validation, all-or-nothing updates, shared recorded time, settlement completeness, and idempotent replay.
3. Protect captures/refunds from correction and retain settlement membership and historical statement truth.
4. Re-run the full accumulated suite and adversarially probe overlapping batch/single corrections, refund/correction races, mixed settlement membership, and failure rollback.

**Gate P4:** only an accepted Stage 4 candidate with clean-container, import/export, concurrency, and history-preservation evidence becomes `stage-4/`.

### Phase 5 — Submission evidence and final release audit

1. Download the complete, unfiltered room session and inspect it for credentials before placing the sanitized required copy at `room.json`; rotate any exposed credential rather than merely hiding it.
2. Verify every room seat has a correctly named generic mandate containing actual harness/model metadata.
3. On a fresh clone, run `python -m harness check <result-repo> --track pocketful` and `python -m harness run --repo <clone> --all --mode isolated`.
4. Manually execute every submitted stage's RUN.md in a clean environment and inspect the browser product.
5. Assemble the evidence index, intervention log, video containing actual room work, README, FACTORY.md, and public remote receipt.
6. The selected non-author release verifier issues the final evidence-only verdict. Publish only the evidence-supported stages and claims.

## Non-negotiable acceptance and rejection rules

- A source test, a finite UI state, or a successful local process is not a stage acceptance receipt.
- A missing, stale, or unverifiable financial context is a neutral/uncertain Riptide state, never a factual recommendation.
- No seat accepts production work it authored. Flint reports reproducible failure; Wren owns the release verdict.
- A failure in an earlier stage caps every later stage. Later-stage source must never be backfilled into an earlier snapshot.
- A green shipped-test run is necessary diagnostic evidence, not proof against held-out checks. Each exit requires a specification-derived adversarial review.
- No credentials, remote runtime APIs, CDNs, analytics, or hosted data stores may be required to run a submitted stage.
- Any human correction, infrastructure intervention, or deviation from the room-generated path is recorded with reason, files, and re-verification evidence.

## Evidence ledger per work item

Every handoff records: stable task id; producer and next consumer seat; explicit requirements; affected interface; changed paths; committed revision; commands and test results; artifacts/hashes; assumptions; unresolved risks; and status. A rejection must additionally record criterion, reproduction, observed result, expected behavior, and evidence path.

## Immediate next actions once Phase 0 is closed

1. Sable produces the Stage 1 observable requirement, ambiguity, risk, and evidence matrix from the complete official spec.
2. Vex translates that matrix into bounded contracts and routes service, UI, and integration work without overlapping ownership.
3. Morrow, Vale, and Rook execute only the scoped handoffs in the result repository.
4. The selected breaker attempts to falsify the integrated Stage 1 candidate; corrections return to the owning seat through an explicit room handoff.
5. The selected non-author release verifier independently decides whether the candidate may be snapshotted.

## Planning decision log

| Decision | Rationale | Status |
| --- | --- | --- |
| Treat the current repository as immutable kickoff input | The official guide says the submitted result is a separate repository. | Confirmed. |
| Sequence stages rather than building a final service first | Earlier folders are tested for exact-stage boundaries and score gates are cumulative. | Confirmed. |
| Keep Riptide non-authoritative and local | It must enhance presentation without contradicting financial truth or needing network access. | Confirmed. |
| Use factory gates before dispatch | Roster, generic mandate, clean-container, and task-packet defects are disqualifiers, not polish items. | Confirmed. |
| Use the PDF's four-role architecture as a recommended factory shape, not a mandatory seven-seat roster | The PDF and participant guide require generic independence and a minimum of three configured coding seats; neither makes the previous seven-seat/named-owner plan authoritative. | Confirmed. |
| Record the owner-selected five-seat map before dispatch, but retain a freeze gate | Vex, Morrow, Flint, Wren, and Vale are now the provisional selected seats. Their recorded metadata has material gaps, so the selection is not evidence of a valid judged run. | Confirmed selection; freeze blocked. |
| Treat the new live five-seat document as authoritative for the selected roster, but not as a self-validating freeze artifact | Its five-seat sections agree with the owner-provided freeze record; its surviving seven-seat references and nonexistent-path claims conflict with the current checkout and observed Wren runtime. | Reconciliation required before dispatch. |
| Keep the Bundle checkout as authoritative source material, but not as an unproven submission result | It currently contains the kickoff specs, harness, participant guide, and planning PDF; it does not yet contain the required result-repository factory artifacts or accepted stage folders. | Confirmed. |
| Select result-repository path, public remote, and actual seat/runtime details | These are currently absent from verified room/workspace evidence. | Pending human/seat-owner confirmation. |

## Plan maintenance

Update this plan only when official requirements, reachable-seat status, the selected result-repository boundary, or accepted architecture decisions change. Record rejected alternatives and their evidence rather than rewriting them out of history.
