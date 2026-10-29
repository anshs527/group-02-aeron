# Architecture

## Foundation implemented in source

Two C++20 processes communicate over Aeron IPC using generated SBE Ping and Pong
messages. `hft_common` is a shared library containing CLI validation and a small
validated protocol adapter; retry policy and basic statistics are shared headers.
`hft_transport` is a small tool support library linking the real Aeron C++ wrapper
and C client. It is not an exchange transport framework. One outstanding request
at a time makes correlation, completion, and timeout behavior straightforward.
The native Media Driver and both clients use one explicit, private Aeron directory.
Build/runtime verification status is recorded separately in verification.md.

## Planned trading data path

1. **Trader**: deterministic command producer; correlates exchange replies and
   consumes market data. Future workloads measure infrastructure, not alpha.
2. **Gateway**: validates SBE application contracts, establishes client identity,
   rejects invalid commands, applies admission policy, and routes commands.
3. **Matching engine**: a transport-independent, single-symbol price-time-priority
   order book first. Integer ticks and quantities; deterministic outcomes. It
   produces acceptance/rejection, fill and cancel events from ordered commands.
4. **Market-data service**: turns sequenced matching events into a documented
   feed. Snapshot, incremental update, gap detection and recovery semantics
   must be designed before multi-consumer experiments.
5. **Experiment controller**: Python control plane outside trading processes.
   Starts isolated runs, supplies configuration/workload seeds, collects results,
   compares repeated trials, and retains or rolls back allowed configurations.

Planned flow: trader → gateway → matching engine → gateway → trader. Matching
engine events also feed market-data service → market-data consumers. The
controller configures and observes these processes via files/administrative
interfaces; it does not sit synchronously on the per-order path. Prometheus
export, dashboards, experiment storage and diagnosis stay outside the latency
critical loop. Blocking metrics writes, orchestration calls, allocations and
synchronous logging should not be added to future measured hot paths.

## Aeron model

An **Aeron client** provides publications (send) and subscriptions (poll receive).
A **Media Driver** manages shared buffers, flow control and, for network channels,
network I/O. Multiple local clients can use the same driver when their configured
directory matches. The directory holds the command-and-control file and log
buffers; it is neither a message address nor a durability guarantee.

A channel URI describes transport; a **stream ID** identifies a stream within
that channel, and a publication has a session. This foundation fixes the channel
to `aeron:ipc` and uses separate request/response streams (1001/1002 by default).
Both processes publish and subscribe. Each full application frame is only 16
bytes, below IPC fragmentation size; unexpected lengths fail before decoding.
Larger future messages need fragment reassembly and explicit resource limits.

IPC uses shared memory/mapped buffers on one Linux instance. UDP experiments
will use `aeron:udp` endpoints and one explicitly configured driver directory
per machine/VM, with addresses, ports, MTU and flow-control settings recorded.
Do not share one filesystem path across machines and expect that to create a
network route. UDP is not implemented by these tools yet.

An accepted publication offer means the message entered Aeron's buffer. It is
not confirmation that a matching engine processed, committed or durably stored
an order. Transport delivery does **not** establish application-level exactly-once
processing or durability. The smoke test's correlation check only validates one
isolated ordered request/reply session. It is not duplicate suppression.

Before a trading integration, specify command sequencing scope, client session
identity and epochs, duplicate detection keys/windows, replay behavior,
acknowledgment meaning, restart fencing, and recovery from gaps. Durable journal,
snapshot and replay are separate future decisions. Retries after ambiguous
application failures need explicit idempotency rules; do not blindly resubmit a
command after transport reconnect and assume it cannot execute twice.

## Deployment boundaries

Initial execution is one Linux host. Local distributed experiments later use
Vagrant/VirtualBox only after checking host architecture and virtualization
support; WSL2 and nested virtualization require separate evaluation. Terraform
and GCP Compute Engine are later deployment work, with cost limits, teardown and
secret handling designed before provisioning. No VM/cloud resources are created
by this foundation.
