// A native library for hermes-jsi-run: `add(a, b)` runs on the JavaScript thread; `double(x)` returns a Promise that a
// std::thread resolves later, through the host's `post` (headless_hermes.h). Licensed under the Apache License 2.0.
#include <chrono>
#include <memory>
#include <thread>

#include <hermes/hermes.h>

#include "headless_hermes.h"  // in the prebuilt include/, or ../ in this repository

using namespace facebook;

extern "C" void headless_hermes_register(jsi::Runtime &rt, const headless_hermes_host *host) {
  rt.global().setProperty(
      rt, "add",
      jsi::Function::createFromHostFunction(rt, jsi::PropNameID::forAscii(rt, "add"), 2,
                                            [](jsi::Runtime &, const jsi::Value &, const jsi::Value *args, size_t) {
                                              return jsi::Value(args[0].asNumber() + args[1].asNumber());
                                            }));

  rt.global().setProperty(
      rt, "double",
      jsi::Function::createFromHostFunction(
          rt, jsi::PropNameID::forAscii(rt, "double"), 1,
          [host](jsi::Runtime &rt, const jsi::Value &, const jsi::Value *args, size_t) {
            double x = args[0].asNumber();
            auto executor = jsi::Function::createFromHostFunction(
                rt, jsi::PropNameID::forAscii(rt, "executor"), 2,
                [host, x](jsi::Runtime &rt, const jsi::Value &, const jsi::Value *args, size_t) {
                  // The resolve function may only be touched on the JavaScript thread: keep it there.
                  auto resolve = std::make_shared<jsi::Value>(rt, args[0]);
                  host->ref();
                  std::thread([host, x, resolve] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    double result = x * 2;  // the work, off the JavaScript thread
                    headless_hermes::post(host, [host, resolve, result](jsi::Runtime &rt) {
                      resolve->asObject(rt).asFunction(rt).call(rt, result);
                      host->unref();
                    });
                  }).detach();
                  return jsi::Value::undefined();
                });
            return rt.global().getPropertyAsFunction(rt, "Promise").callAsConstructor(rt, executor);
          }));
}
