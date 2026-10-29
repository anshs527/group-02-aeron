# Contributor and agent instructions

- Use C++20. Ubuntu 24.04 LTS is the reference runtime; prefer WSL2 on Windows.
- Keep matching logic independent of transport when it is added.
- Use integer price ticks and integer quantities; never floating-point order prices.
- Do not silently lose commands on backpressure. Retry within a deadline or report failure.
- Keep allocations and synchronous logging out of future measured hot paths.
- Keep generated code in the build tree, separate from handwritten code. Change the schema and regenerate; never edit codecs.
- Keep dependency releases/commits pinned and update docs/dependencies.md with changes.
- Run checks appropriate to changes: formatting, build, unit tests, codec generation, and IPC tests for transport changes.
- Never claim unexecuted checks passed. Record environmental blockers precisely.
- Keep this foundation small. Matching, workload, controller, VM, and cloud features require separate implementation work.
- Preserve others' work. Do not commit, push, add a remote, provision resources, install system packages, or select a license without instruction.
