# napi-host

Standalone Node-API host for Hermes.

Loads `.node` addons via `hermes_napi_load_module` (exposing each addon on `globalThis` under its filename without `.node`), executes a JavaScript file via `hermes_run_script`, and runs an event loop supporting:

- `console` (formatted as Node formats the common cases) and `print(...)`
- `require()` for `.js`, `.json` and `.node` files, resolved as Node does: relative paths, `node_modules` upward, a
  package's `main`, `index` files; `module`, `exports`, `__filename`, `__dirname`; `process.platform`, `process.arch`,
  `process.argv`. The script itself runs as the main module.
- `setTimeout` / `clearTimeout`
- `Promise` microtasks (`RuntimeConfig::Builder().withMicrotaskQueue(true)` and `Runtime::drainJobs()`)
- Worker-pool `post_work` / `cancel_work` (`napi_create_async_work`, `napi_queue_async_work`, `napi_cancel_async_work`)
- Thread-safe `post_task` and `ref_loop` / `unref_loop` (`napi_create_threadsafe_function`)

## Usage

```sh
./napi-host <script.js> [<addon.node> ...] [-- <script arguments>]
```

Addons given on the command line are also exposed as globals, named after the file. Arguments after `--` are the
script's, in `process.argv` after the host and the script. [`require-demo/`](require-demo) requires a file, a JSON
file, a package and an addon:

```sh
./napi-host require-demo/main.js -- path/to/hello.node
```

## Building standalone

```sh
cmake -S napi-host -B build-napi-host -G Ninja \
  -DHERMES_SRC_DIR=/path/to/hermes \
  -DHERMES_BUILD_DIR=/path/to/hermes-build
cmake --build build-napi-host
```
