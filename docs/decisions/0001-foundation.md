# ADR 0001: local Linux foundation

Status: implementation chosen; full execution validation pending prerequisites.

Use C++20 on Ubuntu 24.04 and CMake-managed pinned dependencies. Use the supported
Aeron C++ wrapper over the C client and the native driver from the same release.
Generate codecs at build time from the checked-in schema using an explicit SBE
JAR release and Java 17+. Keep generated files out of version control.

Keep one request in flight in the IPC correctness tool. Use separate streams,
strict lengths/headers/correlation IDs, bounded startup and response deadlines,
and explicit offer-status classification. Retry temporary disconnection,
backpressure and administrative action; fail closed on fatal/unknown statuses.
The harness owns a unique runtime directory and only its own child processes.

Report count/min/mean/max RTT initially. No invented percentiles, no latency
thresholds in CI. The tool is not an open-loop performance benchmark. HdrHistogram,
workload metadata and controlled measurements are task 6 rather than a superficial
histogram added to a closed-loop test and presented as a benchmark.

Defer matching, trading integration, optimizer, VMs and cloud deployment. Keep
future matching logic transport-independent. Do not select a team license now.
