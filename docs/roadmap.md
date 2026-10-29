# Ordered implementation roadmap

Only task 1's source implementation is present. Task 1 verification remains
blocked on the authoring environment (verification.md). The NewOrder example is
a starting point for task 2, not completion of the trading protocol.

1. **Build and Aeron smoke test.** Complete the current verification on Ubuntu
   24.04. Acceptance: clean Debug/Release/ASan builds generate real SBE codecs;
   GoogleTest and all native-driver positive/negative IPC cases pass; formatter
   passes; failed/missing peers return nonzero within deadlines; CI retains logs.
   No performance acceptance threshold on shared runners.
2. **Trading protocol contracts.** Add NewOrder/cancel/replace, acceptance/reject,
   fill and market-data schemas with versioning policy. Acceptance: document
   tick/lot definitions, valid ranges, sequencing scope, client epochs, duplicate
   keys, retry semantics and acknowledgment meaning; golden frames, malformed
   inputs and compatibility tests pass using generated codecs.
3. **Single-symbol price-time-priority order book.** Implement deterministic
   transport-independent matching with integer ticks/quantities. Acceptance:
   tests cover FIFO within a price, best-price priority, partial/multiple fills,
   cancel/replace priority policy, invalid/duplicate orders, overflow prevention,
   empty book and invariants; deterministic command logs reproduce state/events.
4. **Gateway → matching engine → client integration.** Route validated SBE
   commands and correlated replies over IPC. Acceptance: deterministic scenario
   compares all replies/fills against an oracle; backpressure does not lose
   commands; gaps, duplicates, disconnects and process restart have documented
   failure/recovery behavior; no durability/exactly-once claim without evidence.
5. **Market-data consumer.** Publish sequenced incremental events and define
   snapshot semantics. Acceptance: a consumer reconstructs the expected book,
   detects a gap, rejects stale/duplicate updates, and recovers via the defined
   snapshot/resubscription procedure; tests reconcile feed and engine state.
6. **Deterministic workload and latency histograms.** Add fixed-seed, open-loop
   scheduled arrivals and a pinned HdrHistogram integration. Acceptance: record
   offered/achieved load, errors, scheduled and actual send times, throughput,
   warm-up and repeated runs; correlation accounting reconciles all commands;
   histogram units/range/precision/overflow are tested, metadata is saved, and
   coordinated omission limitations are explained. Establish a controlled baseline.
7. **UDP across local VMs.** Validate host/hypervisor/architecture support before
   selecting Vagrant boxes/provider. Acceptance: repeatable multi-VM bring-up and
   teardown, explicit driver directories/endpoints/ports, UDP correctness under
   loss/reordering/slow consumers, timeout behavior and recorded clock uncertainty.
   VirtualBox compatibility is not assumed, including on ARM or WSL2.
8. **Prometheus/Grafana.** Export bounded-cardinality throughput/error/backpressure
   metrics and build dashboards. Acceptance: monitoring matches test counts,
   restart/staleness are visible, exporter failures cannot block matching, and
   controlled comparisons quantify collection overhead.
9. **Experiment runner and rollback.** Implement Python orchestration over a
   bounded allowlist of configurations. Acceptance: baseline/candidate repeated
   runs save commit/config/hardware/dependency/workload metadata; incomplete runs
   are rejected; crash/interruption restores the prior known-good configuration;
   a dry run and injected failure prove rollback. Predefine acceptance criteria
   before running experiments; don't optimize noise or correctness violations.
10. **GCP deployment.** Choose and pin Terraform/provider/images only when ready.
    Acceptance: reviewed cost/quota/region/network/IAM plan, no committed secrets,
    reproducible provisioning and teardown, controlled UDP correctness, metadata
    parity with local runs, explicit user authorization before provisioning.
11. **Optional higher-level diagnosis.** Add explainable bottleneck diagnosis
    after trustworthy measurements and rollback exist. Acceptance: suggestions
    cite measured evidence, remain within controller constraints, are evaluated
    against held-out scenarios and are never treated as trading signals.

Parallel team ownership can start after shared contracts are agreed, but do not
skip dependency acceptance gates. Suggested next three tasks: unblock task 1's
verification, finish task 2's protocol contracts, then implement task 3's book.

Pending non-feature decision: team license selection before publication.
