// headless_hermes.h: what hermes-jsi-run gives the native libraries it loads.
//
// A library exports one of:
//
//   extern "C" void headless_hermes_register(facebook::jsi::Runtime &rt, const headless_hermes_host *host);
//   extern "C" void registerNatives(facebook::jsi::Runtime &rt);   // no host: synchronous functions only
//
// `host` lives as long as the process. Its functions may be called from any thread: `post` queues a task that runs on
// the JavaScript thread (microtasks are drained after it), which is how a result computed on another thread gets back
// to JavaScript; `ref` / `unref` keep the event loop alive while such work is pending (the process exits when no
// timers, no posted tasks and no refs remain). This is the role React Native's CallInvoker plays in an app.
//
// Licensed under the Apache License 2.0 (see LICENSE).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// A task posted to the JavaScript thread: `runtime` is the `facebook::jsi::Runtime *`.
typedef void (*headless_hermes_task)(void *runtime, void *data);

typedef struct headless_hermes_host {
  /// 1 for this layout; fields are only ever added at the end.
  int version;
  /// Queue `task(runtime, data)` on the JavaScript thread. Any thread.
  void (*post)(headless_hermes_task task, void *data);
  /// Keep the event loop alive until the matching `unref`. Any thread.
  void (*ref)(void);
  void (*unref)(void);
} headless_hermes_host;

#ifdef __cplusplus
}

#include <functional>
#include <memory>

namespace facebook::jsi {
class Runtime;
}

namespace headless_hermes {

/// `host->post` with a C++ callable.
inline void post(const headless_hermes_host *host, std::function<void(facebook::jsi::Runtime &)> fn) {
  using Fn = std::function<void(facebook::jsi::Runtime &)>;
  host->post(
      [](void *rt, void *data) {
        std::unique_ptr<Fn> f(static_cast<Fn *>(data));
        (*f)(*static_cast<facebook::jsi::Runtime *>(rt));
      },
      new Fn(std::move(fn)));
}

}  // namespace headless_hermes
#endif
