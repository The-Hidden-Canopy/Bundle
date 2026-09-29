# Financial truth and provenance

BUNDLE keeps four distinct layers:

1. **Source truth** — what a bank, file, user, or provider reported.
2. **Normalized truth** — canonical accounts, balances, and transactions.
3. **Inferred state** — categories, recurring candidates, and expected events.
4. **Planning state** — budgets, goals, buffers, and planned events.

Derived forecast, safe-to-spend, budget, and recurring-candidate results can
be retained as append-only calculation receipts. A receipt records the engine,
policy, input references, input hash, and serialized outputs; it is evidence of
how a result was calculated, not proof that an external balance is current.

Source references and transaction revisions are append-only. A new provider
payload creates a new observation or revision; it does not silently rewrite
history. Forecast, safe-to-spend, insight, and scenario outputs are projections
with a basis, not replacement financial truth.

Admission requires source content hashes in the form `sha256:<64 hex
characters>`. A changed source object must advance its source version and
append a transaction revision; a same-version content change is rejected.

Unknown and stale inputs remain explicit. A stale observation cannot be labeled
fresh merely because a projection was calculated from it.
