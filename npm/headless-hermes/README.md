# @qalqi/headless-hermes

**Hermes, without the app.** The JavaScript engine behind React Native, as a plain command and a C++ library: no
Metro, no simulator, no device. One `npm i`, and Hermes runs on your laptop and in CI.

[![npm](https://img.shields.io/npm/v/@qalqi/headless-hermes)](https://www.npmjs.com/package/@qalqi/headless-hermes)
[![build](https://github.com/qalqi/headless-hermes-jsi-node-api/actions/workflows/build.yml/badge.svg)](https://github.com/qalqi/headless-hermes-jsi-node-api/actions/workflows/build.yml)

```sh
npm i -D @qalqi/headless-hermes
```

```js
// fib.js
const fib = n => n < 2 ? n : fib(n - 1) + fib(n - 2);
print(`fib(25) = ${fib(25)} on Hermes ${HermesInternal.getRuntimeProperties()["OSS Release Version"]}`);
```

```console
$ npx hermes fib.js
fib(25) = 75025 on Hermes 1.0.0
```

## Does my code run on Hermes?

Node and browsers are not Hermes. Find out on your machine, not on a phone:

```js
// probe.js
const check = (name, f) => { try { f(); print(`  ok   ${name}`); } catch (e) { print(`  no   ${name}: ${e.message}`); } };
check("class private fields", () => { class A { #x = 1; get() { return this.#x; } } new A().get(); });
check("Intl.DateTimeFormat", () => new Intl.DateTimeFormat("en").format(0));
check("with statement", () => new Function("with ({}) {}")());
check("WeakRef", () => new WeakRef({}));
check("structuredClone", () => structuredClone({}));
```

```console
$ npx hermes probe.js
  ok   class private fields
  no   Intl.DateTimeFormat: Property 'Intl' doesn't exist
  no   with statement: 1:14:with statement is not supported
  ok   WeakRef
  no   structuredClone: Property 'structuredClone' doesn't exist
```

The same check in GitHub Actions, no install step:

```yaml
- run: npx -y -p @qalqi/headless-hermes hermes test/hermes-smoke.js
```

## Compile to bytecode

`hermesc` ships too. Precompile once, start instantly:

```console
$ npx hermesc -emit-binary -out fib.hbc fib.js
$ npx hermes fib.hbc
fib(25) = 75025 on Hermes 1.0.0
```

## Embed Hermes in C++ in 13 lines

The package carries the JSI headers and the runtime library, so a native host is one compiler call away:

```cpp
// host.cpp
#include <hermes/hermes.h>
#include <iostream>
using namespace facebook;
int main() {
  auto rt = facebook::hermes::makeHermesRuntime();
  rt->global().setProperty(*rt, "twice", jsi::Function::createFromHostFunction(
      *rt, jsi::PropNameID::forAscii(*rt, "twice"), 1,
      [](jsi::Runtime &, const jsi::Value &, const jsi::Value *args, size_t) {
        return jsi::Value(args[0].getNumber() * 2);
      }));
  auto r = rt->evaluateJavaScript(std::make_shared<jsi::StringBuffer>("twice(21)"), "main.js");
  std::cout << "twice(21) = " << r.getNumber() << std::endl;
}
```

```console
$ INC=$(node -p "require('@qalqi/headless-hermes').includeDir()")
$ LIB=$(node -p "require('@qalqi/headless-hermes').libDir()")
$ g++ -std=c++17 host.cpp -I"$INC" -L"$LIB" -lhermesvm -Wl,-rpath,"$LIB" -o host
$ ./host
twice(21) = 42
```

That is how a JSI native module gets tested without React Native: register it on a headless runtime, run its
JavaScript, assert. The [repository](https://github.com/qalqi/headless-hermes-jsi-node-api) has more hosts to copy:
host functions loaded from shared libraries, an event loop with `setTimeout` and microtasks, and a Node-API host that
loads unmodified Node.js addons (C and [napi-rs](https://napi.rs/)) into Hermes.

## What you get

| Platform | Package (installed for you) |
|---|---|
| Linux x64 | [`@qalqi/headless-hermes-linux-x64`](https://www.npmjs.com/package/@qalqi/headless-hermes-linux-x64) |
| Linux arm64 | [`@qalqi/headless-hermes-linux-arm64`](https://www.npmjs.com/package/@qalqi/headless-hermes-linux-arm64) |
| macOS arm64 | [`@qalqi/headless-hermes-darwin-arm64`](https://www.npmjs.com/package/@qalqi/headless-hermes-darwin-arm64) |

Each has `bin/hermes`, `bin/hermesc`, `lib/libhermesvm` and `include/` (JSI, Hermes, Node-API). npm picks the one for
your machine; you only ever install `@qalqi/headless-hermes`.

```js
const hermes = require('@qalqi/headless-hermes');
hermes.includeDir();     // headers
hermes.libDir();         // libhermesvm
hermes.bin('hermesc');   // path to a tool
hermes.buildInfo();      // the exact Hermes commit
```

## Which Hermes?

A pinned commit of Hermes' `static_h` branch, the one with JSI and Node-API (it reports itself as Hermes 1.0.0).
`buildInfo()` prints the commit; the repository's README maps every release to its Hermes commit. Every version is
built and published by GitHub Actions with [npm provenance](https://docs.npmjs.com/generating-provenance-statements),
so each tarball links to the run and source that built it.

Only need `hermesc` to compile bytecode for a React Native release build? Use Meta's
[`hermes-compiler`](https://www.npmjs.com/package/hermes-compiler). This package is for running and embedding Hermes.

## License

Packaging: Apache-2.0. Hermes: MIT, Meta Platforms, Inc. and affiliates (`LICENSE-hermes` in each platform package).
Started from [tmikov/hermes-jsi-demos](https://github.com/tmikov/hermes-jsi-demos).
