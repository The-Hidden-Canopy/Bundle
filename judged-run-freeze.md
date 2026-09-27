# Judged Run Freeze Record

Status: **NOT FROZEN — DISPATCH PROHIBITED**

Recorded: 2026-09-26

This document records only values observed from the selected runtime seats or the current workspace. `UNVERIFIED` is intentional: it is a blocker, not a value to replace by inference.

## Selected seats

| Seat | Judged-run role | Actual declaration status |
| --- | --- | --- |
| Vex | Planner / Architect | Active in the room; local runtime metadata partially observable. |
| Morrow | Builder / Integrator | Declared by the active runtime seat. |
| Flint | Independent Breaker / Test Engineer | Active in the room; runtime declaration has not been supplied. |
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
| Wren | opencode | `opencode/big-pickle` | Band ACP turn, `wren` session, profile `default`, Windows PowerShell 5.1 | opencode with Jam ACP runtime and private Task MCP | N/A at present | `E:\HiddenCanopy\Bundle` |
| Vale | Anthropic | `claude-sonnet-4-6` | Claude Code CLI using Band.ai agent SDK/MCP transport | Band.ai agent platform | `UNVERIFIED` — no browser is available and UI tooling is not provisioned | `D:\THC-code\Bundle\agent` |

## Paths and commands

| Field | Actual value | Status |
| --- | --- | --- |
| Task packet path | `E:\HiddenCanopy\Bundle\pocketful\spec\stage-1.md` is the official Stage 1 source specification. | Source exists; **no frozen dispatch packet has been assembled**. |
| Result-output path | `UNVERIFIED` | No result root has been selected. |
| Clean build command | `UNVERIFIED` | No candidate stage folder or Dockerfile exists. |
| Clean start command | `UNVERIFIED` | No candidate stage folder or RUN.md exists. |
| Health/readiness check | `UNVERIFIED` | No candidate service exists to check. |
| BAND room export path | `UNVERIFIED` | Export destination has not been selected. |
| Run manifest path | `UNVERIFIED` | `kickoff-manifest.json` is a source-integrity manifest, not a judged-run manifest. |
| Evidence index path | `UNVERIFIED` | No judged candidate or evidence index exists. |

## Freeze checks

| Check | Status | Evidence / blocker |
| --- | --- | --- |
| Five selected mandates hashed | BLOCKED | No mandate files have been created and audited against the selected runtime seats. |
| Task packet hashed | BLOCKED | Official source spec exists, but the frozen run-specific dispatch packet has not been assembled. |
| Model/runtime metadata recorded | BLOCKED | Flint metadata is unverified; Vex exact deployment ID is not exposed. |
| Clean build tested | BLOCKED | No stage candidate or documented build command exists. |
| Clean start tested | BLOCKED | No stage candidate or documented start command exists. |
| No outbound dependency verified | BLOCKED | Requires a built candidate and isolated run. |
| Result-output path verified | BLOCKED | No output path selected. |
| Room export location verified | BLOCKED | No export location selected. |
| Run manifest initialized | BLOCKED | No judged-run manifest exists. |
| No task-specific language appears in standing mandates | BLOCKED | No standing mandates exist to audit. |

## Dispatch decision

Do not dispatch a judged stage. This record must be updated only with observed values and command receipts; it may be frozen only after every check above is PASS and every selected mandate is generic, correctly named, and mapped to its active room seat.
