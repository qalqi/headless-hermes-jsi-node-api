# napi-host

Standalone Node-API host for Hermes.

Loads `.node` addons via `hermes_napi_load_module` (exposing each addon on `globalThis` under its filename without `.node`), executes a JavaScript file via `hermes_run_script`, and runs an event loop supporting:

- `print(...)`
- `setTimeout` / `clearTimeout`
- `Promise` microtasks (`RuntimeConfig::Builder().withMicrotaskQueue(true)` and `Runtime::drainJobs()`)
- Worker-pool `post_work` / `cancel_work` (`napi_create_async_work`, `napi_queue_async_work`, `napi_cancel_async_work`)
- Thread-safe `post_task` and `ref_loop` / `unref_loop` (`napi_create_threadsafe_function`)

## Usage

```sh
./napi-host <script.js> [<addon.node> ...]
```

## Building standalone

```sh
cmake -S napi-host -B build-napi-host -G Ninja \
  -DHERMES_SRC_DIR=/path/to/hermes \
  -DHERMES_BUILD_DIR=/path/to/hermes-build
cmake --build build-napi-host
```
