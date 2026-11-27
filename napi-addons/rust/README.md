# napi-addons/rust

A minimal [napi-rs](https://napi.rs/) addon built with Cargo and loaded unmodified into Hermes via `napi-host`.

Exports:
- `sum(a, b)` -> `a + b`
- `Counter(initial)` class with `add(delta)` method

## Build & Run

```sh
cargo build --release --manifest-path napi-addons/rust/Cargo.toml --target-dir build/napi-addons/rust/target
# On Linux:
cp build/napi-addons/rust/target/release/librust_addon.so build/napi-addons/rust/rust_addon.node
# On macOS:
cp build/napi-addons/rust/target/release/librust_addon.dylib build/napi-addons/rust/rust_addon.node

./build/napi-host/napi-host napi-addons/rust/demo.js ./build/napi-addons/rust/rust_addon.node
```
