# hft-infra-optimizer

UIUC IE421: **Autonomous Low-Latency Trading Infrastructure Optimizer**.
Our four-person team studies trading infrastructure performance, not predictive
trading strategies. The eventual system will measure a simplified electronic
exchange, experiment with infrastructure settings, and retain or roll back
changes using reproducible evidence.

This repository contains the initial foundation: a C++20 shared common library,
real Aeron IPC ping/pong tools, a versioned SBE schema, generated-code adapters,
GoogleTest tests, a bounded native-driver integration harness, and Linux CI.
**Full build/runtime verification is currently blocked on the authoring host**;
see [the verification record](docs/verification.md) before treating this as a
validated baseline. No mocked transport stands in for Aeron.

The matching engine, trading client, market-data service, workload generator,
autonomous controller, monitoring, VM provisioning, and cloud deployment are
planned only. See [architecture](docs/architecture.md), [protocol](docs/protocol.md),
[measurement](docs/measurement.md), and the [ordered roadmap](docs/roadmap.md).
No project license has been selected; that is a team decision.

## Prerequisites

Reference environment: **Ubuntu 24.04 LTS, Linux x86-64**. Linux arm64 is an
intended later validation target, not a verified platform. Windows teammates
should use WSL2 and keep the checkout in its Linux filesystem. Native Windows
and macOS builds are not supported by this foundation. VirtualBox support must
be checked for the particular host OS, CPU architecture, and hypervisor setup;
it is not required here.

- GCC 13+ or Clang 18+ with a C++20 standard library, C compiler, and pthreads.
- CMake **3.30+** (Aeron 1.48.0 requires this); local bootstrap pins **3.31.6**.
- Ninja, Git, and Java **17+** runtime for the **SBE generator**, not the driver.
- Bash, GNU coreutils (`timeout`, `mktemp`, `realpath`), curl, CA certificates.
- clang-format **18** for formatting checks.
- Internet access on first configuration to GitHub and Maven Central, or a
  pre-populated dependency cache as described in [dependencies](docs/dependencies.md).

No Python, Prometheus, Grafana, Terraform, VirtualBox, Java Media Driver, or
system-installed Aeron is required to build/run these tools. Native driver
optional BSD/UUID libraries are discovered by upstream; their fallbacks suffice.

On Ubuntu, these are **manual commands for a teammate to choose to run**; no
repository script installs system packages:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends build-essential git ninja-build openjdk-17-jre-headless clang-format-18 curl ca-certificates
```

Ubuntu 24.04's default CMake is too old. If your CMake is below 3.30, use the
optional, repository-local binary bootstrap (no sudo), then set its path:

```bash
bash scripts/local_cmake.sh
export PATH="$PWD/.tools/cmake-3.31.6-linux-$(uname -m)/bin:$PATH"
```

## Clean-checkout build and tests

Starting in a fresh `hft-infra-optimizer` checkout, with prerequisites available:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -L unit
ctest --preset debug -L integration
bash scripts/ipc_smoke.sh build/debug
bash scripts/format.sh --check
```

`ctest --preset debug` runs both labels. Codecs are generated from
`schemas/trading.xml` into `build/debug/generated/hft_sbe/` automatically.
To explicitly request generation: `cmake --build build/debug --target sbe_codecs`.
Editing the schema triggers regeneration; never copy or hand-edit generated files.

Other configurations:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

ASan/UBSan instrument our code and generated codec accesses in our translation
units. The upstream C client and native driver are not instrumented by this
preset. All builds use warnings as errors on handwritten targets only.

A successful happy-path smoke run prints the following shape (numbers vary):

```text
FUNCTIONAL VALIDATION ONLY; closed-loop RTT, not a performance benchmark
PING OK count=1000 warmup=100 min_ns=<observed> mean_ns=<observed> max_ns=<observed>
PONG OK count=1100
IPC happy PASS (functional validation only)
IPC logs: .../build/debug/logs/ipc-happy.<unique>
```

Those placeholders are not measured results. No latency thresholds or percentile
claims are made. The harness uses a fresh `/tmp/hft-ipc.*` directory, launches
`bin/aeronmd_s` (the **native C Media Driver**), and stops only its own children.
It retains logs in the build directory and deletes only its own temporary runtime.
Tests also require clear failures for no driver, no peer, a connected peer that
never replies, truncated responses, wrong correlation IDs, and wrong message
templates. They do not simulate Aeron.

## Manual native driver and tools

For an interactive run, create one new directory and share the exact same path
across all three terminals. Do not reuse another process's Aeron directory.

Terminal 1, from the repository root:

```bash
mkdir -p .runtime
run_root=$(mktemp -d "$PWD/.runtime/manual.XXXXXXXX")
printf 'Use this --aeron-dir in both clients: %s/aeron\n' "$run_root"
env AERON_DIR="$run_root/aeron" AERON_THREADING_MODE=SHARED AERON_DIR_DELETE_ON_START=false AERON_DIR_DELETE_ON_SHUTDOWN=false AERON_IPC_TERM_BUFFER_LENGTH=65536 build/debug/bin/aeronmd_s
```

Terminal 2 (replace the absolute path with the printed value):

```bash
build/debug/bin/aeron_pong --aeron-dir /absolute/printed/path/aeron --messages 1000 --warmup 100 --startup-timeout-ms 60000
```

Terminal 3, within 60 seconds, with the same path and counts:

```bash
build/debug/bin/aeron_ping --aeron-dir /absolute/printed/path/aeron --messages 1000 --warmup 100 --startup-timeout-ms 60000
```

Pong and ping exit after the configured counts. Stop the foreground driver with
Ctrl-C, then remove only the newly created `run_root` if desired. Stream defaults
are request **1001** and response **1002**; override `--request-stream` and
`--response-stream` on both peers. One ping/pong pair per isolated directory is
supported. `--response-timeout-ms` defaults to 2000 and includes offer retries
and response reception. `--help` lists arguments. `configs/ipc.env.example`
documents the harness's driver environment.

## Troubleshooting

- Missing Ninja/Java/formatter: install the explicit prerequisite yourself.
  `JAVA_HOME` may be set to a Java 17+ installation; confirm `java -version`.
- CMake too old: use `scripts/local_cmake.sh` or an existing CMake >=3.30.
- Fetch failures: check DNS/TLS/proxy access to GitHub and Maven Central. Do not
  replace dependencies with unpinned branches. Use documented offline paths.
- Startup timeout: verify the native driver is alive, all directory paths agree,
  stream IDs are distinct and paired, and both clients run before the deadline.
  Read driver/ping/pong logs. The harness isolates each run so stale files from
  another driver are not removed.
- Response timeout: check peer count/warm-up agreement, driver errors, disk space,
  process scheduling, and backpressure. A timeout fails the run; it is not a
  latency sample. Increase timeouts only for functional diagnosis.
- Permission/shared-memory errors: choose a directory writable by all intended
  processes under the same user. The harness uses `/tmp` rather than depending
  on container `/dev/shm` capacity. Never use a broadly shared default directory.
- Wrong template/correlation/length: this is a protocol failure, not a successful
  sample. Use a dedicated pair and investigate the sender.
- Slow or noisy RTT: expected with yielding, shared driver threads, instrumentation,
  debug/sanitizer builds, or CI. See measurement.md before benchmarking.

## Team workflow

Read `AGENTS.md` and the roadmap. Suggested ownership is transport/build,
protocol/order book, workload/measurement, and orchestration/observability;
review contracts together before splitting integration work. Work on small
branches, run relevant checks, include the commands and results in reviews, and
record dependency/protocol decisions. Format handwritten changes with
`bash scripts/format.sh --fix`. Keep matching logic transport-independent and
use integer ticks/quantities. Do not check in credentials, build trees, generated
code, or large measurement outputs. Select a license together before publishing.
This local repository has no remote and no initial commit.
