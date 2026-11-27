// hermes-jsi-run: run a script on Hermes with native JSI libraries loaded into the runtime, timers, `console`, and an
// event loop that native code on other threads can post back to (headless_hermes.h).
//
//   hermes-jsi-run <script.js> [<library> ...]
//
// Each library exports `headless_hermes_register(jsi::Runtime &, const headless_hermes_host *)` or
// `registerNatives(jsi::Runtime &)`; both are called, in the order given, before the script runs. The process exits
// when no timers, posted tasks or refs remain: 0, or 1 after printing an uncaught exception to stderr.
//
// Started from tmikov/hermes-jsi-demos' evloop and hf-runner (public domain); licensed under the Apache License 2.0.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <hermes/hermes.h>

#include "headless_hermes.h"

using namespace facebook;

static const char *kConsole =
#include "console.js.inc"
    ;
static const char *kJslib =
#include "jslib.js.inc"
    ;

// The loop's queue of tasks posted from any thread, and its refs.
static std::mutex gMutex;
static std::condition_variable gWake;
static std::deque<std::pair<headless_hermes_task, void *>> gPosted;
static std::atomic<int> gRefs{0};

static void hostPost(headless_hermes_task task, void *data) {
  {
    std::lock_guard<std::mutex> lock(gMutex);
    gPosted.emplace_back(task, data);
  }
  gWake.notify_one();
}
static void hostRef() { gRefs++; }
static void hostUnref() {
  gRefs--;
  gWake.notify_one();
}
static const headless_hermes_host kHost = {1, hostPost, hostRef, hostUnref};

static double nowMs() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::optional<std::string> readFile(const char *path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

using RegisterHost = void (*)(jsi::Runtime &, const headless_hermes_host *);
using RegisterNatives = void (*)(jsi::Runtime &);

static bool loadLibrary(jsi::Runtime &rt, const char *path) {
#ifndef _WIN32
  void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    std::cerr << "hermes-jsi-run: cannot load " << path << ": " << dlerror() << std::endl;
    return false;
  }
  if (auto f = reinterpret_cast<RegisterHost>(dlsym(lib, "headless_hermes_register"))) {
    f(rt, &kHost);
    return true;
  }
  if (auto f = reinterpret_cast<RegisterNatives>(dlsym(lib, "registerNatives"))) {
    f(rt);
    return true;
  }
  std::cerr << "hermes-jsi-run: " << path << " exports neither headless_hermes_register nor registerNatives" << std::endl;
  return false;
#else
  std::cerr << "hermes-jsi-run: loading libraries isn't supported on Windows yet" << std::endl;
  return false;
#endif
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <script.js> [<library> ...]" << std::endl;
    return 2;
  }
  const char *script = argv[1];
  auto code = readFile(script);
  if (!code) {
    std::cerr << "hermes-jsi-run: cannot read " << script << std::endl;
    return 2;
  }

  auto config = ::hermes::vm::RuntimeConfig::Builder().withMicrotaskQueue(true).build();
  auto runtime = facebook::hermes::makeHermesRuntime(config);
  jsi::Runtime &rt = *runtime;

  try {
    auto write = jsi::Function::createFromHostFunction(
        rt, jsi::PropNameID::forAscii(rt, "write"), 2,
        [](jsi::Runtime &rt, const jsi::Value &, const jsi::Value *args, size_t count) {
          if (count < 2) return jsi::Value::undefined();
          std::string text = args[1].toString(rt).utf8(rt);
          FILE *f = args[0].asNumber() == 2 ? stderr : stdout;
          fwrite(text.data(), 1, text.size(), f);
          return jsi::Value::undefined();
        });
    rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>(kConsole), "hermes-jsi-run:console.js")
        .asObject(rt)
        .asFunction(rt)
        .call(rt, std::move(write));
    auto loop = rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>(kJslib), "hermes-jsi-run:jslib.js")
                    .asObject(rt)
                    .asFunction(rt)
                    .call(rt)
                    .asObject(rt);
    auto peek = loop.getPropertyAsFunction(rt, "peek");
    auto run = loop.getPropertyAsFunction(rt, "run");
    run.call(rt, nowMs());

    for (int i = 2; i < argc; i++)
      if (!loadLibrary(rt, argv[i])) return 1;

    rt.evaluateJavaScript(std::make_shared<jsi::StringBuffer>(std::move(*code)), script);
    rt.drainMicrotasks();

    for (;;) {
      // Tasks posted from other threads first, each followed by its microtasks.
      std::deque<std::pair<headless_hermes_task, void *>> posted;
      {
        std::lock_guard<std::mutex> lock(gMutex);
        posted.swap(gPosted);
      }
      for (auto &[task, data] : posted) {
        task(&rt, data);
        rt.drainMicrotasks();
      }
      if (!posted.empty()) {
        fflush(stdout);
        continue;
      }

      double next = peek.call(rt).getNumber();
      double now = nowMs();
      if (next >= 0 && next <= now) {
        run.call(rt, now);
        rt.drainMicrotasks();
        continue;
      }

      std::unique_lock<std::mutex> lock(gMutex);
      if (!gPosted.empty()) continue;
      if (next < 0) {
        if (gRefs.load() <= 0) break;
        gWake.wait(lock, [] { return !gPosted.empty() || gRefs.load() <= 0; });
      } else {
        auto until = std::chrono::steady_clock::now() + std::chrono::duration<double, std::milli>(next - now);
        gWake.wait_until(lock, std::chrono::time_point_cast<std::chrono::steady_clock::duration>(until),
                         [] { return !gPosted.empty(); });
      }
    }
  } catch (jsi::JSError &e) {
    fflush(stdout);
    std::cerr << "Uncaught " << e.getStack() << std::endl;
    return 1;
  } catch (jsi::JSIException &e) {
    fflush(stdout);
    std::cerr << "hermes-jsi-run: " << e.what() << std::endl;
    return 1;
  }
  fflush(stdout);
  return 0;
}
