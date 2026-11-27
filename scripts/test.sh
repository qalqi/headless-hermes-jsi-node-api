#!/usr/bin/env bash
# Run every demo and compare its standard output with expected/<demo>.txt.
#   DEMOS_BUILD_DIR   where the demos were built (default: build)
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${DEMOS_BUILD_DIR:-$root/build}"
case "$(uname)" in Darwin) so=dylib ;; *) so=so ;; esac
failed=0

check_expected() {
  local name="$1" expected="$2"; shift 2
  local actual
  actual="$("$@" 2>/dev/null)"
  if diff -u "$root/expected/$expected.txt" <(printf '%s\n' "$actual") > "$out/$name.diff"; then
    echo "ok    $name"
  else
    echo "FAIL  $name"; cat "$out/$name.diff"; failed=1
  fi
}

check() {
  local name="$1"; shift
  check_expected "$name" "$name" "$@"
}

check          hello          "$out/hello/hello"
check          runner         "$out/runner/runner" "$root/runner/hello.js"
check          host-functions "$out/host-functions/host-functions" "$root/host-functions/demo.js"
check          hf-runner      "$out/hf-runner/hf-runner" "$root/hf-runner/demo.js" \
                              "$out/hf-runner/libhfadd.$so" "$out/hf-runner/libhfmyprint.$so"
check          evloop         "$out/evloop/evloop" "$root/evloop/timer.js"
check          jsi-run-console "$out/jsi-run/hermes-jsi-run" "$root/jsi-run/example/console.js"
check          jsi-run        "$out/jsi-run/hermes-jsi-run" "$root/jsi-run/example/demo.js" \
                              "$out/jsi-run/libjsirunworker.$so"
check          napi-host      "$out/napi-host/napi-host" "$root/napi-host/demo.js"
check          napi-hello     "$out/napi-host/napi-host" "$root/napi-addons/hello/demo.js" \
                              "$out/napi-addons/hello/hello.node"
check          napi-async     "$out/napi-host/napi-host" "$root/napi-addons/async/demo.js" \
                              "$out/napi-addons/async/async_addon.node"
check          napi-rust      "$out/napi-host/napi-host" "$root/napi-addons/rust/demo.js" \
                              "$out/napi-addons/rust/rust_addon.node"
check_expected compare-jsi    compare "$out/compare/compare-jsi"  "$root/compare/demo.js"
check_expected compare-napi   compare "$out/compare/compare-napi" "$root/compare/demo.js"
exit $failed
