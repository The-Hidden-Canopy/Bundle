# Contributing to BUNDLE

Thank you for helping improve BUNDLE. The project is local-first and
open-source under Apache License 2.0. Contributions should preserve the Open
Canopy Contract and the financial engineering specification.

Before opening a change:

- keep financial truth append-first and preserve source provenance;
- keep provider connectors read-only and optional;
- do not add credentials, raw personal financial data, telemetry, or network
  requirements to the core;
- prefer a small, reviewable change with an adversarial test for the boundary
  it changes;
- describe what changed, what evidence supports it, what remains unproven, and
  how to roll it back.

Run the local gates from the repository root:

```powershell
cmake -S . -B build -DBUNDLE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

$env:PYTHONPATH = "connectors"
py -3 -m unittest discover -s connectors/tests -v
```

Do not include live account data in issues, pull requests, fixtures, or test
output. Use synthetic data with explicit timestamps and source references.
