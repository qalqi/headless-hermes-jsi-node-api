#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "hermes/Public/RuntimeConfig.h"
#include "hermes/VM/Runtime.h"
#include "napi/hermes_napi.h"

namespace {

// common/console.js and prelude.js (require, process), embedded as C++ raw string literals.
const char *kConsole =
#include "console.js.inc"
    ;
const char *kPrelude =
#include "prelude.js.inc"
    ;

std::optional<std::string> readFile(const char *path) {
  std::ifstream fileStream(path);
  if (!fileStream) {
    std::cerr << path << ": error opening file\n";
    return std::nullopt;
  }
  std::stringstream stringStream;
  stringStream << fileStream.rdbuf();
  return stringStream.str();
}

std::string addonGlobalName(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  std::string name =
      (slash == std::string::npos) ? path : path.substr(slash + 1);
  const std::string ext = ".node";
  if (name.size() > ext.size() &&
      name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
    name.erase(name.size() - ext.size());
  }
  return name;
}

std::string napiValueToUtf8(napi_env env, napi_value value) {
  napi_value strVal = nullptr;
  if (napi_coerce_to_string(env, value, &strVal) != napi_ok) {
    napi_value ignored = nullptr;
    napi_get_and_clear_last_exception(env, &ignored);
    return "(could not convert value to string)";
  }
  size_t len = 0;
  if (napi_get_value_string_utf8(env, strVal, nullptr, 0, &len) != napi_ok) {
    return "(could not read string)";
  }
  std::string buf(len + 1, '\0');
  if (napi_get_value_string_utf8(env, strVal, &buf[0], len + 1, &len) !=
      napi_ok) {
    return "(could not read string)";
  }
  buf.resize(len);
  return buf;
}

bool printAndClearException(napi_env env) {
  bool hasPending = false;
  if (napi_is_exception_pending(env, &hasPending) != napi_ok || !hasPending) {
    return false;
  }

  napi_value exception = nullptr;
  napi_get_and_clear_last_exception(env, &exception);

  napi_valuetype type = napi_undefined;
  if (napi_typeof(env, exception, &type) == napi_ok && type == napi_object) {
    bool hasStack = false;
    if (napi_has_named_property(env, exception, "stack", &hasStack) == napi_ok &&
        hasStack) {
      napi_value stackVal = nullptr;
      if (napi_get_named_property(env, exception, "stack", &stackVal) ==
          napi_ok) {
        napi_valuetype stackType = napi_undefined;
        if (napi_typeof(env, stackVal, &stackType) == napi_ok &&
            stackType == napi_string) {
          std::string stack = napiValueToUtf8(env, stackVal);
          if (!stack.empty()) {
            std::cerr << stack << "\n";
            return true;
          }
        }
      }
    }
  }

  std::cerr << "Error: " << napiValueToUtf8(env, exception) << "\n";
  return true;
}

class EventLoop {
 public:
  explicit EventLoop(size_t workerCount = 0) {
    if (workerCount == 0) {
      unsigned hw = std::thread::hardware_concurrency();
      workerCount = std::min(4u, std::max(2u, hw));
    }
    workers_.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
      workers_.emplace_back([this]() { workerMain(); });
    }
  }

  ~EventLoop() {
    shutdownWorkers();
  }

  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;

  hermes_napi_host makeHost() {
    hermes_napi_host host{};
    host.post_work = &EventLoop::onPostWork;
    host.cancel_work = &EventLoop::onCancelWork;
    host.post_task = &EventLoop::onPostTask;
    host.data = this;
    host.uv_loop = nullptr;
    host.fatal_exception = &EventLoop::onFatalException;
    host.ref_loop = &EventLoop::onRefLoop;
    host.unref_loop = &EventLoop::onUnrefLoop;
    return host;
  }

  void shutdownWorkers() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (stopWorkers_) {
        return;
      }
      stopWorkers_ = true;
    }
    workCv_.notify_all();
    for (auto &t : workers_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  uint64_t addTimer(
      napi_env env,
      napi_value cb,
      double delayMs,
      const std::vector<napi_value> &args) {
    napi_ref cbRef = nullptr;
    if (napi_create_reference(env, cb, 1, &cbRef) != napi_ok) {
      return 0;
    }
    std::vector<napi_ref> argRefs;
    argRefs.reserve(args.size());
    for (napi_value arg : args) {
      napi_ref ref = nullptr;
      if (napi_create_reference(env, arg, 1, &ref) != napi_ok) {
        napi_delete_reference(env, cbRef);
        for (napi_ref r : argRefs) {
          napi_delete_reference(env, r);
        }
        return 0;
      }
      argRefs.push_back(ref);
    }

    int64_t ms = 0;
    if (std::isfinite(delayMs) && delayMs > 0.0) {
      constexpr double kMaxMs = 365.0 * 24.0 * 3600.0 * 1000.0;
      ms = static_cast<int64_t>(std::min(delayMs, kMaxMs));
    }

    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

    std::lock_guard<std::mutex> lock(mu_);
    uint64_t id = nextTimerId_++;
    timers_.push_back(Timer{id, deadline, cbRef, std::move(argRefs)});
    loopCv_.notify_one();
    return id;
  }

  void cancelTimer(napi_env env, uint64_t id) {
    Timer removed{};
    bool found = false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->id == id) {
          removed = std::move(*it);
          timers_.erase(it);
          found = true;
          break;
        }
      }
    }
    if (found) {
      releaseTimerRefs(env, removed);
      loopCv_.notify_one();
    }
  }

  void clearTimers(napi_env env) {
    std::vector<Timer> leftover;
    {
      std::lock_guard<std::mutex> lock(mu_);
      leftover.swap(timers_);
    }
    for (auto &t : leftover) {
      releaseTimerRefs(env, t);
    }
  }

  bool run(napi_env env, hermes::vm::Runtime &runtime) {
    if (!drainMicrotasks(env, runtime)) {
      return false;
    }

    for (;;) {
      std::function<void()> task;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (fatalError_) {
          return false;
        }
        if (!taskQueue_.empty()) {
          task = std::move(taskQueue_.front());
          taskQueue_.pop_front();
        }
      }

      if (task) {
        task();
        if (!checkPostCallback(env, runtime)) {
          return false;
        }
        continue;
      }

      Timer dueTimer{};
      bool hasDueTimer = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!timers_.empty()) {
          auto earliest = findEarliestTimer();
          if (earliest->deadline <= std::chrono::steady_clock::now()) {
            dueTimer = std::move(*earliest);
            timers_.erase(earliest);
            hasDueTimer = true;
          }
        }
      }

      if (hasDueTimer) {
        bool ok = fireTimer(env, dueTimer);
        if (!ok || !checkPostCallback(env, runtime)) {
          return false;
        }
        continue;
      }

      std::unique_lock<std::mutex> lock(mu_);
      if (fatalError_) {
        return false;
      }
      if (taskQueue_.empty() && timers_.empty() && activeWorks_ == 0 &&
          loopRefs_ == 0) {
        return true;
      }

      if (timers_.empty()) {
        loopCv_.wait(lock, [this]() {
          return !taskQueue_.empty() || fatalError_ ||
              (timers_.empty() && activeWorks_ == 0 && loopRefs_ == 0);
        });
      } else {
        auto deadline = findEarliestTimer()->deadline;
        loopCv_.wait_until(lock, deadline, [this]() {
          return !taskQueue_.empty() || fatalError_;
        });
      }
    }
  }

 private:
  struct WorkItem {
    void *work_data = nullptr;
    void (*execute)(void *work_data) = nullptr;
    void (*complete)(void *work_data, napi_status status) = nullptr;
    bool started = false;
    bool cancelled = false;
  };

  struct Timer {
    uint64_t id = 0;
    std::chrono::steady_clock::time_point deadline;
    napi_ref cbRef = nullptr;
    std::vector<napi_ref> argRefs;
  };

  std::vector<Timer>::iterator findEarliestTimer() {
    return std::min_element(
        timers_.begin(),
        timers_.end(),
        [](const Timer &a, const Timer &b) {
          if (a.deadline != b.deadline) {
            return a.deadline < b.deadline;
          }
          return a.id < b.id;
        });
  }

  static void releaseTimerRefs(napi_env env, Timer &timer) {
    if (timer.cbRef) {
      napi_delete_reference(env, timer.cbRef);
      timer.cbRef = nullptr;
    }
    for (napi_ref ref : timer.argRefs) {
      if (ref) {
        napi_delete_reference(env, ref);
      }
    }
    timer.argRefs.clear();
  }

  bool fireTimer(napi_env env, Timer &timer) {
    napi_handle_scope scope = nullptr;
    if (napi_open_handle_scope(env, &scope) != napi_ok) {
      releaseTimerRefs(env, timer);
      return false;
    }

    napi_value cb = nullptr;
    napi_status st = napi_get_reference_value(env, timer.cbRef, &cb);
    std::vector<napi_value> argv;
    argv.reserve(timer.argRefs.size());
    if (st == napi_ok) {
      for (napi_ref ref : timer.argRefs) {
        napi_value val = nullptr;
        st = napi_get_reference_value(env, ref, &val);
        if (st != napi_ok) {
          break;
        }
        argv.push_back(val);
      }
    }

    releaseTimerRefs(env, timer);

    bool ok = true;
    if (st == napi_ok && cb != nullptr) {
      napi_value recv = nullptr;
      napi_get_undefined(env, &recv);
      if (napi_call_function(
              env,
              recv,
              cb,
              argv.size(),
              argv.empty() ? nullptr : argv.data(),
              nullptr) != napi_ok) {
        printAndClearException(env);
        ok = false;
      }
    }

    napi_close_handle_scope(env, scope);
    return ok;
  }

  bool drainMicrotasks(napi_env env, hermes::vm::Runtime &runtime) {
    if (runtime.drainJobs() == hermes::vm::ExecutionStatus::EXCEPTION) {
      runtime.clearThrownValue();
      std::cerr << "Error: uncaught exception in microtask\n";
      return false;
    }
    bool hasPending = false;
    if (napi_is_exception_pending(env, &hasPending) == napi_ok && hasPending) {
      napi_handle_scope scope = nullptr;
      napi_open_handle_scope(env, &scope);
      printAndClearException(env);
      napi_close_handle_scope(env, scope);
      return false;
    }
    return true;
  }

  bool checkPostCallback(napi_env env, hermes::vm::Runtime &runtime) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (fatalError_) {
        return false;
      }
    }
    bool hasPending = false;
    if (napi_is_exception_pending(env, &hasPending) == napi_ok && hasPending) {
      napi_handle_scope scope = nullptr;
      napi_open_handle_scope(env, &scope);
      printAndClearException(env);
      napi_close_handle_scope(env, scope);
      return false;
    }
    return drainMicrotasks(env, runtime);
  }

  void workerMain() {
    for (;;) {
      std::shared_ptr<WorkItem> item;
      {
        std::unique_lock<std::mutex> lock(mu_);
        workCv_.wait(
            lock, [this]() { return stopWorkers_ || !workQueue_.empty(); });
        if (stopWorkers_) {
          return;
        }
        item = workQueue_.front();
        workQueue_.pop_front();
        if (item->cancelled) {
          continue;
        }
        item->started = true;
      }

      if (item->execute) {
        item->execute(item->work_data);
      }

      {
        std::lock_guard<std::mutex> lock(mu_);
        taskQueue_.push_back([this, item]() {
          if (item->complete) {
            item->complete(item->work_data, napi_ok);
          }
          std::lock_guard<std::mutex> lk(mu_);
          --activeWorks_;
        });
      }
      loopCv_.notify_one();
    }
  }

  static void onPostWork(
      void *loop_data,
      void *work_data,
      void (*execute)(void *work_data),
      void (*complete)(void *work_data, napi_status status)) {
    auto *self = static_cast<EventLoop *>(loop_data);
    auto item = std::make_shared<WorkItem>();
    item->work_data = work_data;
    item->execute = execute;
    item->complete = complete;

    {
      std::lock_guard<std::mutex> lock(self->mu_);
      self->workQueue_.push_back(item);
      ++self->activeWorks_;
    }
    self->workCv_.notify_one();
    self->loopCv_.notify_one();
  }

  static bool onCancelWork(void *loop_data, void *work_data) {
    auto *self = static_cast<EventLoop *>(loop_data);
    std::lock_guard<std::mutex> lock(self->mu_);
    for (auto &item : self->workQueue_) {
      if (item->work_data == work_data && !item->cancelled && !item->started) {
        item->cancelled = true;
        self->taskQueue_.push_back([self, item]() {
          if (item->complete) {
            item->complete(item->work_data, napi_cancelled);
          }
          std::lock_guard<std::mutex> lk(self->mu_);
          --self->activeWorks_;
        });
        self->loopCv_.notify_one();
        return true;
      }
    }
    return false;
  }

  static void onPostTask(
      void *loop_data,
      void *task_data,
      void (*callback)(void *task_data)) {
    auto *self = static_cast<EventLoop *>(loop_data);
    {
      std::lock_guard<std::mutex> lock(self->mu_);
      self->taskQueue_.push_back(
          [callback, task_data]() { callback(task_data); });
    }
    self->loopCv_.notify_one();
  }

  static void onFatalException(void *loop_data, napi_env env, napi_value err) {
    auto *self = static_cast<EventLoop *>(loop_data);
    std::cerr << "Error: " << napiValueToUtf8(env, err) << "\n";
    {
      std::lock_guard<std::mutex> lock(self->mu_);
      self->fatalError_ = true;
    }
    self->loopCv_.notify_one();
  }

  static void onRefLoop(void *loop_data) {
    auto *self = static_cast<EventLoop *>(loop_data);
    std::lock_guard<std::mutex> lock(self->mu_);
    ++self->loopRefs_;
  }

  static void onUnrefLoop(void *loop_data) {
    auto *self = static_cast<EventLoop *>(loop_data);
    {
      std::lock_guard<std::mutex> lock(self->mu_);
      --self->loopRefs_;
    }
    self->loopCv_.notify_one();
  }

  std::mutex mu_;
  std::condition_variable workCv_;
  std::condition_variable loopCv_;
  std::vector<std::thread> workers_;
  std::deque<std::shared_ptr<WorkItem>> workQueue_;
  std::deque<std::function<void()>> taskQueue_;
  std::vector<Timer> timers_;
  uint64_t nextTimerId_ = 1;
  size_t activeWorks_ = 0;
  int loopRefs_ = 0;
  bool stopWorkers_ = false;
  bool fatalError_ = false;
};

napi_value jsPrint(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
    return nullptr;
  }
  std::vector<napi_value> argv(argc);
  if (argc > 0 &&
      napi_get_cb_info(env, info, &argc, argv.data(), nullptr, nullptr) !=
          napi_ok) {
    return nullptr;
  }

  for (size_t i = 0; i < argc; ++i) {
    if (i > 0) {
      std::cout << ' ';
    }
    napi_value strVal = nullptr;
    if (napi_coerce_to_string(env, argv[i], &strVal) != napi_ok) {
      return nullptr;
    }
    size_t len = 0;
    if (napi_get_value_string_utf8(env, strVal, nullptr, 0, &len) != napi_ok) {
      return nullptr;
    }
    std::string buf(len + 1, '\0');
    if (napi_get_value_string_utf8(env, strVal, &buf[0], len + 1, &len) !=
        napi_ok) {
      return nullptr;
    }
    buf.resize(len);
    std::cout << buf;
  }
  std::cout << std::endl;
  return nullptr;
}

napi_value jsSetTimeout(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  void *data = nullptr;
  if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data) != napi_ok) {
    return nullptr;
  }
  std::vector<napi_value> argv(argc);
  if (argc > 0 &&
      napi_get_cb_info(env, info, &argc, argv.data(), nullptr, nullptr) !=
          napi_ok) {
    return nullptr;
  }

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "setTimeout requires a callback");
    return nullptr;
  }
  napi_valuetype cbType = napi_undefined;
  if (napi_typeof(env, argv[0], &cbType) != napi_ok ||
      cbType != napi_function) {
    napi_throw_type_error(
        env, nullptr, "setTimeout callback must be a function");
    return nullptr;
  }

  double delayMs = 0.0;
  if (argc >= 2) {
    napi_value numVal = nullptr;
    if (napi_coerce_to_number(env, argv[1], &numVal) != napi_ok) {
      return nullptr;
    }
    napi_get_value_double(env, numVal, &delayMs);
  }

  std::vector<napi_value> extraArgs;
  if (argc > 2) {
    extraArgs.assign(argv.begin() + 2, argv.end());
  }

  auto *loop = static_cast<EventLoop *>(data);
  uint64_t id = loop->addTimer(env, argv[0], delayMs, extraArgs);
  if (id == 0) {
    napi_throw_error(env, nullptr, "Failed to schedule timer");
    return nullptr;
  }

  napi_value result = nullptr;
  napi_create_double(env, static_cast<double>(id), &result);
  return result;
}

napi_value jsClearTimeout(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1]{};
  void *data = nullptr;
  if (napi_get_cb_info(env, info, &argc, argv, nullptr, &data) != napi_ok) {
    return nullptr;
  }
  if (argc < 1) {
    return nullptr;
  }

  napi_valuetype type = napi_undefined;
  if (napi_typeof(env, argv[0], &type) != napi_ok || type != napi_number) {
    return nullptr;
  }

  double idDouble = 0.0;
  if (napi_get_value_double(env, argv[0], &idDouble) != napi_ok ||
      !std::isfinite(idDouble) || idDouble <= 0.0) {
    return nullptr;
  }

  auto *loop = static_cast<EventLoop *>(data);
  loop->cancelTimer(env, static_cast<uint64_t>(idDouble));
  return nullptr;
}

// The native helpers prelude.js and console.js call.
std::string argString(napi_env env, napi_callback_info info, size_t i, napi_value *argvOut = nullptr) {
  size_t argc = 4;
  napi_value argv[4]{};
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  if (argvOut) {
    for (size_t k = 0; k < 4; k++) argvOut[k] = argv[k];
  }
  return i < argc ? napiValueToUtf8(env, argv[i]) : std::string();
}

napi_value makeString(napi_env env, const std::string &s) {
  napi_value v = nullptr;
  napi_create_string_utf8(env, s.data(), s.size(), &v);
  return v;
}

napi_value jsWrite(napi_env env, napi_callback_info info) {
  napi_value argv[4]{};
  std::string text = argString(env, info, 1, argv);
  int32_t fd = 1;
  napi_get_value_int32(env, argv[0], &fd);
  std::cout.flush();
  fwrite(text.data(), 1, text.size(), fd == 2 ? stderr : stdout);
  fflush(fd == 2 ? stderr : stdout);
  return nullptr;
}

napi_value jsReadFile(napi_env env, napi_callback_info info) {
  std::string path = argString(env, info, 0);
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    napi_throw_error(env, "ENOENT", ("cannot read " + path).c_str());
    return nullptr;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return makeString(env, ss.str());
}

napi_value jsStat(napi_env env, napi_callback_info info) {
  std::string path = argString(env, info, 0);
  struct stat st {};
  int kind = stat(path.c_str(), &st) != 0 ? 0 : S_ISDIR(st.st_mode) ? 2 : 1;
  napi_value v = nullptr;
  napi_create_int32(env, kind, &v);
  return v;
}

napi_value jsRealpath(napi_env env, napi_callback_info info) {
  std::string path = argString(env, info, 0);
  char buf[PATH_MAX];
  return makeString(env, realpath(path.c_str(), buf) ? std::string(buf) : path);
}

napi_value jsCwd(napi_env env, napi_callback_info) {
  char buf[PATH_MAX];
  return makeString(env, getcwd(buf, sizeof buf) ? std::string(buf) : std::string("/"));
}

napi_value jsLoadAddon(napi_env env, napi_callback_info info) {
  std::string path = argString(env, info, 0);
  napi_value exports = nullptr;
  if (hermes_napi_load_module(env, path.c_str(), &exports) != napi_ok) {
    bool pending = false;
    napi_is_exception_pending(env, &pending);
    if (!pending) napi_throw_error(env, nullptr, ("cannot load the addon " + path).c_str());
    return nullptr;
  }
  return exports;
}

/// compile(source, filename): the module's source wrapped in its CommonJS function, on the same first line so
/// stack traces keep their line numbers.
napi_value jsCompile(napi_env env, napi_callback_info info) {
  std::string source = argString(env, info, 0);
  std::string filename = argString(env, info, 1);
  std::string wrapped = "(function (exports, require, module, __filename, __dirname) {" + source + "\n})";
  hermes_run_script_flags flags{};
  flags.struct_size = sizeof(flags);
  napi_value result = nullptr;
  if (hermes_run_script(env, reinterpret_cast<const uint8_t *>(wrapped.c_str()), wrapped.size() + 1, nullptr,
                        nullptr, filename.c_str(), &flags, &result) != napi_ok) {
    return nullptr;
  }
  return result;
}

/// Runs an embedded script that evaluates to a function, and calls it with `arg`; returns what it returns.
napi_value callEmbedded(napi_env env, const char *source, const char *name, napi_value arg) {
  hermes_run_script_flags flags{};
  flags.struct_size = sizeof(flags);
  napi_value fn = nullptr, global = nullptr, result = nullptr;
  if (hermes_run_script(env, reinterpret_cast<const uint8_t *>(source), strlen(source) + 1, nullptr, nullptr, name,
                        &flags, &fn) != napi_ok ||
      napi_get_global(env, &global) != napi_ok ||
      napi_call_function(env, global, fn, 1, &arg, &result) != napi_ok) {
    return nullptr;
  }
  return result;
}

/// Installs console and process, and returns prelude.js's runMain.
napi_value installPrelude(napi_env env, const std::vector<std::string> &args) {
  napi_value native = nullptr, fn = nullptr, arr = nullptr;
  napi_create_object(env, &native);
  auto set = [&](const char *name, napi_callback cb) {
    napi_create_function(env, name, NAPI_AUTO_LENGTH, cb, nullptr, &fn);
    napi_set_named_property(env, native, name, fn);
  };
  set("write", jsWrite);
  set("readFile", jsReadFile);
  set("stat", jsStat);
  set("realpath", jsRealpath);
  set("cwd", jsCwd);
  set("loadAddon", jsLoadAddon);
  set("compile", jsCompile);
#if defined(__APPLE__)
  napi_set_named_property(env, native, "platform", makeString(env, "darwin"));
#else
  napi_set_named_property(env, native, "platform", makeString(env, "linux"));
#endif
#if defined(__aarch64__) || defined(__arm64__)
  napi_set_named_property(env, native, "arch", makeString(env, "arm64"));
#else
  napi_set_named_property(env, native, "arch", makeString(env, "x64"));
#endif
  napi_create_array(env, &arr);
  for (size_t i = 0; i < args.size(); i++) napi_set_element(env, arr, static_cast<uint32_t>(i), makeString(env, args[i]));
  napi_set_named_property(env, native, "argv", arr);

  napi_value write = nullptr;
  napi_get_named_property(env, native, "write", &write);
  if (!callEmbedded(env, kConsole, "napi-host:console.js", write)) return nullptr;
  return callEmbedded(env, kPrelude, "napi-host:prelude.js", native);
}

bool registerHostGlobals(napi_env env, EventLoop &loop) {
  napi_handle_scope scope = nullptr;
  if (napi_open_handle_scope(env, &scope) != napi_ok) {
    return false;
  }

  napi_value global = nullptr;
  napi_value printFn = nullptr;
  napi_value setTimeoutFn = nullptr;
  napi_value clearTimeoutFn = nullptr;

  bool ok = napi_get_global(env, &global) == napi_ok &&
      napi_create_function(
          env, "print", NAPI_AUTO_LENGTH, jsPrint, nullptr, &printFn) ==
          napi_ok &&
      napi_set_named_property(env, global, "print", printFn) == napi_ok &&
      napi_create_function(
          env, "setTimeout", NAPI_AUTO_LENGTH, jsSetTimeout, &loop,
          &setTimeoutFn) == napi_ok &&
      napi_set_named_property(env, global, "setTimeout", setTimeoutFn) ==
          napi_ok &&
      napi_create_function(
          env, "clearTimeout", NAPI_AUTO_LENGTH, jsClearTimeout, &loop,
          &clearTimeoutFn) == napi_ok &&
      napi_set_named_property(env, global, "clearTimeout", clearTimeoutFn) ==
          napi_ok;

  napi_close_handle_scope(env, scope);
  return ok;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <script.js> [<addon.node> ...] [-- <script arguments>]\n";
    return 1;
  }

  const char *scriptPath = argv[1];
  auto optCode = readFile(scriptPath);
  if (!optCode) {
    return 1;
  }

  EventLoop loop;
  hermes_napi_host host = loop.makeHost();

  auto rtConfig =
      hermes::vm::RuntimeConfig::Builder().withMicrotaskQueue(true).build();
  auto runtime = hermes::vm::Runtime::create(rtConfig);
  napi_env env = hermes_napi_create_env(runtime.get(), &host);
  if (!env) {
    std::cerr << "Error: failed to create Node-API environment\n";
    return 1;
  }

  if (!registerHostGlobals(env, loop)) {
    std::cerr << "Error: failed to register host globals\n";
    return 1;
  }

  // Addons before "--" are loaded as globals; what follows "--" is the script's (process.argv).
  int firstArg = argc;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--") {
      firstArg = i + 1;
      break;
    }
    const char *addonPath = argv[i];
    napi_handle_scope scope = nullptr;
    if (napi_open_handle_scope(env, &scope) != napi_ok) {
      std::cerr << "Error: failed to open handle scope\n";
      return 1;
    }

    napi_value exports = nullptr;
    napi_status status = hermes_napi_load_module(env, addonPath, &exports);
    if (status != napi_ok) {
      printAndClearException(env);
      napi_close_handle_scope(env, scope);
      return 1;
    }

    napi_value global = nullptr;
    std::string globalName = addonGlobalName(addonPath);
    if (napi_get_global(env, &global) != napi_ok ||
        napi_set_named_property(env, global, globalName.c_str(), exports) !=
            napi_ok) {
      printAndClearException(env);
      napi_close_handle_scope(env, scope);
      return 1;
    }

    napi_close_handle_scope(env, scope);
  }

  int exitCode = 0;
  {
    napi_handle_scope scope = nullptr;
    if (napi_open_handle_scope(env, &scope) != napi_ok) {
      std::cerr << "Error: failed to open handle scope\n";
      return 1;
    }

    // The script runs as the main CommonJS module: require() resolves from its folder.
    std::vector<std::string> args{argv[0], scriptPath};
    for (int i = firstArg; i < argc; i++) args.emplace_back(argv[i]);
    napi_value runMain = installPrelude(env, args);
    napi_value global = nullptr, pathValue = nullptr, result = nullptr;
    if (!runMain || napi_get_global(env, &global) != napi_ok ||
        napi_create_string_utf8(env, scriptPath, NAPI_AUTO_LENGTH, &pathValue) != napi_ok ||
        napi_call_function(env, global, runMain, 1, &pathValue, &result) != napi_ok) {
      printAndClearException(env);
      exitCode = 1;
    }
    napi_close_handle_scope(env, scope);
  }

  if (exitCode == 0 && !loop.run(env, *runtime)) {
    exitCode = 1;
  }

  loop.clearTimers(env);
  loop.shutdownWorkers();
  env = nullptr;
  runtime.reset();
  return exitCode;
}
