# hermes-jsi-run

A ready-made JSI host: runs a script on Hermes with your native libraries loaded into the runtime, `console`,
timers, and an event loop that native code on other threads can post results back to. Test a JSI module on a
desktop or in CI without React Native.

```sh
hermes-jsi-run <script.js> [<library> ...]
```

| The script gets | |
|---|---|
| `console.log/info/debug` (stdout), `warn/error/trace/assert` (stderr), `count`, `time`/`timeEnd` | formatted as Node formats them, for the common cases (`example/console.js` is checked against Node's output) |
| `setTimeout`, `setInterval`, `setImmediate` and their `clear…`, `queueMicrotask` | microtasks are drained after the script and after every task |
| `print` | as in the `hermes` tool |

An uncaught exception prints `Uncaught <stack>` to stderr and exits with 1; otherwise the process exits with 0 once no
timers, posted tasks or refs remain.

## Writing a library

A library exports one function, called before the script runs:

```cpp
#include <hermes/hermes.h>
#include "headless_hermes.h"

extern "C" void headless_hermes_register(facebook::jsi::Runtime &rt, const headless_hermes_host *host) {
  // install host functions and objects into rt.global(); keep `host` for later
}
```

`host->post(task, data)` (or `headless_hermes::post(host, lambda)`) runs a task on the JavaScript thread from any
thread: compute on a worker, post the result back, resolve a Promise there. `host->ref()` / `host->unref()` keep the
loop alive while work is pending. It is the part React Native's CallInvoker plays in an app: a library's
React Native module can wrap `post` in its own invoker. Libraries written for the `hf-runner` example
(`registerNatives(jsi::Runtime &)`) load too, without a host.

[`example/worker.cpp`](example/worker.cpp) does both: `add(a, b)` on the JavaScript thread, and `double(x)` returning a
Promise resolved from a `std::thread`. Built against the prebuilt Hermes from npm:

```sh
INC=$(node -p "require('@qalqi/headless-hermes').includeDir()")
LIB=$(node -p "require('@qalqi/headless-hermes').libDir()")
g++ -std=c++17 -shared -fPIC example/worker.cpp -I"$INC" -L"$LIB" -lhermesvm -o libworker.so
npx hermes-jsi-run example/demo.js ./libworker.so
```

```text
add(2, 3) = 5
script done; the loop keeps running while work is pending
microtask before any timer
awaited { a: 2, b: 4 } [ 6 ] Uint8Array(2) [ 2, 4 ]
double(21) resolved on another thread: 42
timer after 50 ms
```

On macOS use `-dynamiclib` and a `.dylib` instead of `-shared -fPIC` and `.so`.

Started from the `evloop` and `hf-runner` examples (tmikov/hermes-jsi-demos, public domain).
