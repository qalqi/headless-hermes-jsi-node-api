// napi-host's CommonJS: `require()` for .js, .json and .node files (Node-API addons, loaded unmodified), resolved as
// Node resolves them: relative paths, then node_modules folders upward, a package's "main", index files. Called with
// the native helpers; installs `process` and returns `runMain(path)`, which runs the script as the main module.
// Licensed under the Apache License 2.0 (see LICENSE).
(function (native) {
  "use strict";

  function dirname(p) {
    var i = p.lastIndexOf("/");
    return i <= 0 ? "/" : p.slice(0, i);
  }
  function normalize(p) {
    var parts = [];
    p.split("/").forEach(function (s) {
      if (s === "" || s === ".") return;
      if (s === "..") parts.pop();
      else parts.push(s);
    });
    return "/" + parts.join("/");
  }
  function join(base, p) {
    return normalize(p.charAt(0) === "/" ? p : base + "/" + p);
  }

  // native.stat: 0 missing, 1 file, 2 directory.
  function file(p) {
    return native.stat(p) === 1 ? p : null;
  }
  function withExtensions(p) {
    return file(p) || file(p + ".js") || file(p + ".json") || file(p + ".node");
  }
  function directory(p) {
    if (native.stat(p) !== 2) return null;
    var pkg = file(p + "/package.json");
    if (pkg) {
      var main = JSON.parse(native.readFile(pkg)).main;
      if (typeof main === "string") {
        var m = withExtensions(join(p, main)) || withExtensions(join(p, main) + "/index");
        if (m) return m;
      }
    }
    return withExtensions(p + "/index");
  }
  function resolve(request, fromDir) {
    var found = null;
    if (/^(\/|\.\.?(\/|$))/.test(request)) {
      var p = join(fromDir, request);
      found = withExtensions(p) || directory(p);
    } else {
      for (var d = fromDir; ; d = dirname(d)) {
        var q = (d === "/" ? "" : d) + "/node_modules/" + request;
        found = withExtensions(q) || directory(q);
        if (found || d === "/") break;
      }
    }
    if (!found) {
      var e = new Error("Cannot find module '" + request + "' (from " + fromDir + ")");
      e.code = "MODULE_NOT_FOUND";
      throw e;
    }
    return native.realpath(found);
  }

  var cache = Object.create(null);
  function load(path) {
    if (path in cache) return cache[path].exports;
    var module = { id: path, filename: path, exports: {}, loaded: false };
    cache[path] = module;
    try {
      if (/\.node$/.test(path)) {
        module.exports = native.loadAddon(path);
      } else if (/\.json$/.test(path)) {
        module.exports = JSON.parse(native.readFile(path));
      } else {
        var dir = dirname(path);
        var fn = native.compile(native.readFile(path), path);
        fn.call(module.exports, module.exports, makeRequire(dir), module, path, dir);
      }
    } catch (e) {
      delete cache[path];
      throw e;
    }
    module.loaded = true;
    return module.exports;
  }
  function makeRequire(dir) {
    function require(request) {
      if (typeof request !== "string") throw new TypeError("require: the module name must be a string");
      return load(resolve(request, dir));
    }
    require.resolve = function (request) { return resolve(request, dir); };
    require.cache = cache;
    return require;
  }

  if (typeof globalThis.process !== "object") {
    globalThis.process = { platform: native.platform, arch: native.arch, argv: native.argv, env: {} };
  }

  return function runMain(path) {
    load(native.realpath(join(native.cwd(), path)));
  };
});
