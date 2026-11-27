# compare

Side-by-side comparison of the same `add(...numbers)` host function implemented with JSI (`jsi_host.cpp`) and with Node-API (`napi_host.cpp`).

Both hosts execute `demo.js` and produce identical output.

## Run

```sh
./build/compare/compare-jsi compare/demo.js
./build/compare/compare-napi compare/demo.js
```
