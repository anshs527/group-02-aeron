#!/usr/bin/env bash
set -euo pipefail

# Every subprocess is bounded. Only this shell's live jobs and its mktemp
# runtime directory are cleaned up. Logs remain under the selected build tree.
build=$(realpath "${1:-build/debug}")
scenario=${2:-happy}
case "$scenario" in
  happy|no-peer|no-driver|no-response|truncated|wrong-correlation|wrong-template) ;;
  *) echo "Unknown scenario: $scenario" >&2; exit 2 ;;
esac
for tool in timeout mktemp realpath; do
  command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 2; }
done
for binary in aeronmd_s aeron_ping aeron_pong; do
  [[ -x "$build/bin/$binary" ]] || { echo "Missing $build/bin/$binary; build first" >&2; exit 2; }
done
if [[ "$scenario" == truncated || "$scenario" == wrong-* || "$scenario" == no-response ]]; then
  [[ -x "$build/bin/aeron_fault_peer" ]] || { echo "Build tests first" >&2; exit 2; }
fi
mkdir -p "$build/logs"
logs=$(mktemp -d "$build/logs/ipc-$scenario.XXXXXXXX")
runtime=$(mktemp -d /tmp/hft-ipc.XXXXXXXX)
cleanup() {
  status=$?
  trap - EXIT INT TERM
  # jobs -pr lists only this shell's running children, never a name-based match.
  mapfile -t live_jobs < <(jobs -pr)
  if ((${#live_jobs[@]})); then
    kill -TERM "${live_jobs[@]}" 2>/dev/null || true
    for pid in "${live_jobs[@]}"; do wait "$pid" 2>/dev/null || true; done
  fi
  rm -rf -- "$runtime"
  if ((status != 0)); then
    for log in "$logs"/*.log; do
      [[ -f "$log" ]] || continue
      echo "--- $log" >&2
      cat "$log" >&2
    done
  fi
  echo "IPC logs: $logs"
  exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "$scenario" != no-driver ]]; then
  env -i PATH="$PATH" AERON_DIR="$runtime/aeron" AERON_THREADING_MODE=SHARED \
    AERON_DIR_DELETE_ON_START=false AERON_DIR_DELETE_ON_SHUTDOWN=false \
    AERON_IPC_TERM_BUFFER_LENGTH=65536 \
    timeout --kill-after=2s 35s "$build/bin/aeronmd_s" >"$logs/driver.log" 2>&1 &
  driver_pid=$!
  for ((attempt=0; attempt<100; ++attempt)); do
    kill -0 "$driver_pid" 2>/dev/null || { echo "Native driver exited" >&2; exit 1; }
    [[ -s "$runtime/aeron/cnc.dat" ]] && break
    sleep 0.1
  done
  [[ -s "$runtime/aeron/cnc.dat" ]] || { echo "Driver readiness timeout" >&2; exit 1; }
fi

args=(--aeron-dir "$runtime/aeron" --messages 1 --warmup 0
      --startup-timeout-ms 10000 --response-timeout-ms 2000)
peer_pid=
if [[ "$scenario" == happy ]]; then
  args=(--aeron-dir "$runtime/aeron" --messages 1000 --warmup 100
        --startup-timeout-ms 10000 --response-timeout-ms 2000)
  timeout --kill-after=2s 25s "$build/bin/aeron_pong" "${args[@]}" >"$logs/pong.log" 2>&1 &
  peer_pid=$!
elif [[ "$scenario" == no-peer || "$scenario" == no-driver ]]; then
  args+=(--startup-timeout-ms 1000)
else
  timeout --kill-after=2s 25s "$build/bin/aeron_fault_peer" "$runtime/aeron" "$scenario" \
    >"$logs/peer.log" 2>&1 &
  peer_pid=$!
fi

# Background + wait allows the shell's signal traps to run promptly.
timeout --kill-after=2s 25s "$build/bin/aeron_ping" "${args[@]}" >"$logs/ping.log" 2>&1 &
ping_pid=$!
result=0
wait "$ping_pid" || result=$?
if [[ "$scenario" == happy ]]; then
  [[ "$result" == 0 ]] || { echo "Ping failed with exit $result" >&2; exit 1; }
  wait "$peer_pid"
  grep -q '^PING OK count=1000 warmup=100 ' "$logs/ping.log"
  grep -q '^PONG OK count=1100$' "$logs/pong.log"
  cat "$logs/ping.log" "$logs/pong.log"
else
  # A watchdog kill, crash, sanitizer error, or unrelated failure must not pass.
  [[ "$result" == 1 ]] || { echo "Expected ping exit 1, got $result" >&2; exit 1; }
  case "$scenario" in
    no-peer) reason='startup timeout' ;;
    no-driver) reason='driver|Driver|cnc|CnC' ;;
    no-response) reason='response timeout' ;;
    truncated) reason='invalid message length' ;;
    wrong-correlation) reason='correlation mismatch' ;;
    wrong-template) reason='invalid SBE header' ;;
  esac
  grep '^PING FAILED:' "$logs/ping.log" | grep -Eq "$reason"
  if [[ -n "$peer_pid" ]]; then
    wait "$peer_pid"
    grep -q "^FAULT INJECTED $scenario$" "$logs/peer.log"
  fi
fi
echo "IPC $scenario PASS (functional validation only)"
