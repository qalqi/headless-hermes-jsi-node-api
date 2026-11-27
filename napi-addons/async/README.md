# napi-addons/async

A C Node-API addon demonstrating asynchronous worker execution (`napi_create_async_work`, `napi_queue_async_work`, `napi_create_promise`, `napi_resolve_deferred`) and a thread-safe function called from a `pthread` worker (`napi_create_threadsafe_function`, `napi_call_threadsafe_function`, `napi_release_threadsafe_function`).

## Run

```sh
./build/napi-host/napi-host napi-addons/async/demo.js ./build/napi-addons/async/async_addon.node
```
