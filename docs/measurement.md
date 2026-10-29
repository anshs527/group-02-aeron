# Measurement discipline

The current smoke test is functional validation. Ping timestamps immediately
before its offer attempts and immediately after validating the matching Pong,
using `std::chrono::steady_clock`. RTT includes offer retries/backpressure,
transport, pong scheduling and processing, response polling and decoding. Request
encoding happens before the start timestamp. Warm-up exercises the identical
path but does not enter reported count/min/mean/max. Values are nanoseconds as
reported by the monotonic clock, not a claim of nanosecond timer accuracy.

RTT is **not one-way latency**, and dividing by two does not establish one-way
latency when paths or service times differ. Separate machines' steady clocks
have unrelated epochs; wall clocks can jump or drift. NTP/PTP synchronization
must be measured with uncertainty and drift before any cross-host timestamp
subtraction. Prefer same-client RTT initially. Future one-way work must record
synchronization method, offset bounds, timestamp point and clock source.

This closed-loop test keeps one message outstanding. A stall stops new requests,
so requests that would have arrived during the stall are absent: **coordinated
omission**. This biases latency distributions under load. It is not solved by
collecting more samples of this loop. Roadmap task 6 adds an **open-loop** workload
with scheduled send times, an independent arrival schedule, bounded outstanding
requests, explicit overload behavior, and both scheduled-to-response and
send-to-response latency. Never silently drop late commands or exclude errors
from the denominator. If histogram correction is used, explain its assumptions
and preserve the uncorrected distribution too.

Every later experiment must record:

- Git commit and dirty-tree status/diff identity; dependency-manifest, compiler,
  CMake, JRE and OS/kernel versions; build type and sanitizer status.
- Hardware/CPU architecture, model, cores, topology, RAM, NIC, storage, VM type;
  CPU affinity, frequency/governor, power settings, NUMA and competing load.
- Exact effective client/driver/controller configuration, environment, stream IDs,
  channel URIs, timeouts, term sizes, idle strategy and threading mode.
- Workload version and fixed random seed, symbols, size/price distribution,
  arrival process, target offered load, duration, warm-up and measurement window.
- Actual attempted/sent/completed counts, achieved throughput over measured wall
  time, outstanding count, backpressure/retry counts, rejects, timeouts, malformed
  responses, disconnects, missing responses and exit statuses.
- Repeated trials with order randomized where appropriate, baseline and candidate
  runs, per-run results and uncertainty. Keep failed trials and their reasons.
- Histogram range, resolution/significant digits, overflow/underflow counts,
  sample count, aggregation method and units. No unobserved percentile values.

Current output implements only the count and basic RTT statistics; it does not
yet produce that complete experiment record. The driver uses shared threads and
small terms to keep the correctness test lightweight. Ping/pong yield when idle,
and the transport adapter checks an error mutex. These are functional choices,
not a tuned trading hot path. HdrHistogram is a concrete next measurement task,
including tests that values outside its range cannot disappear silently.

Shared GitHub Actions runners verify builds, memory safety in our code, protocol
correctness and functional IPC. They do not establish performance claims. There
are no latency gates. Compare infrastructure changes on controlled hardware only
after a reproducible workload and measurement protocol exist. The future
controller must reject incomplete/error-filled runs and preserve the prior
configuration when evidence is insufficient.
