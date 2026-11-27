#!/usr/bin/env bash
# Check that a prebuilt archive (scripts/package.sh) is all a user needs: unpack it, run its hermes-jsi-run and
# napi-host, then build a JSI library, hermes-jsi-run itself, a Node-API host and a Node-API addon from nothing but its
# include/ and lib/, and compare each run with expected/.
#   check-archive.sh dist/hermes-<commit12>-<os>-<arch>.tar.gz
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
archive="$1"
work="$(mktemp -d)"
tar -xzf "$archive" -C "$work"
h="$(echo "$work"/hermes-*)"
case "$(uname)" in
  Darwin) shared=(-dynamiclib); so=dylib; exe_flags=(-Wl,-export_dynamic); addon_flags=(-undefined dynamic_lookup) ;;
  *)      shared=(-shared -fPIC); so=so; exe_flags=(-rdynamic); addon_flags=() ;;
esac
CXX="${CXX:-c++}"; CC="${CC:-cc}"
failed=0
expect() {  # expect <name> <expected> <command...>
  local name="$1" exp="$2"; shift 2
  if diff -u "$root/expected/$exp.txt" <("$@" 2>/dev/null); then echo "ok    $name"; else echo "FAIL  $name"; failed=1; fi
}

# The packaged tools, as shipped.
"$CXX" -std=c++17 "${shared[@]}" "$root/jsi-run/example/worker.cpp" -I"$h/include" -L"$h/lib" -lhermesvm -o "$work/libworker.$so"
expect "packaged hermes-jsi-run" jsi-run "$h/bin/hermes-jsi-run" "$root/jsi-run/example/demo.js" "$work/libworker.$so"
"$CC" "${shared[@]}" "${addon_flags[@]}" -I"$h/include" "$root/napi-addons/hello/hello.c" -o "$work/hello.node"
expect "packaged napi-host" napi-hello "$h/bin/napi-host" "$root/napi-addons/hello/demo.js" "$work/hello.node"

# Built from the archive alone: hermes-jsi-run (JSI and libhermesvm) ...
{ printf 'R"JSLIB('; cat "$root/jsi-run/jslib.js"; printf ')JSLIB"\n'; } > "$work/jslib.js.inc"
"$CXX" -std=c++17 "$root/jsi-run/jsi-run.cpp" -I"$h/include" -I"$work" -L"$h/lib" -lhermesvm -lpthread \
  -Wl,-rpath,"$h/lib" -o "$work/hermes-jsi-run"
expect "hermes-jsi-run built from the archive" jsi-run "$work/hermes-jsi-run" "$root/jsi-run/example/demo.js" "$work/libworker.$so"
# ... and a Node-API host (the VM's headers and the static libraries).
L="$h/lib"
static=("$L/libhermesNapi.a" "$L/libhermesvm_a.a" "$L/libLLVHSupport.a" "$L/libLLVHDemangle.a" "$L/libboost_context.a")
if [ "$(uname)" = Darwin ]; then
  link=(-Wl,-force_load,"${static[0]}" "${static[@]:1}" -framework CoreFoundation)
else
  link=(-Wl,--whole-archive "${static[0]}" -Wl,--no-whole-archive "${static[@]:1}" -lpthread -ldl -lm)
fi
"$CXX" -std=c++17 -DNDEBUG -DNAPI_VERSION=10 "$root/napi-host/napi-host.cpp" -I"$h/include" "${exe_flags[@]}" "${link[@]}" -o "$work/napi-host"
expect "napi-host built from the archive" napi-hello "$work/napi-host" "$root/napi-addons/hello/demo.js" "$work/hello.node"
exit $failed
