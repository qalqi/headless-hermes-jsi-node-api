# napi-addons/hello

A minimal C Node-API addon compiled against the Node-API headers only (no Hermes VM dependency; symbols resolve from `napi-host` at load time).

Exports:
- `hello()` -> `"hello from Node-API"`
- `add(a, b)` -> `a + b`

## Run

```sh
./build/napi-host/napi-host napi-addons/hello/demo.js ./build/napi-addons/hello/hello.node
```
