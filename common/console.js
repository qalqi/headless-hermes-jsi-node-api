// console for the hosts in this repository (hermes-jsi-run, napi-host): a compact subset of Node's formatting, one
// line per value, depth 2. Called with the native `write(fd, text)`; installs `console` and `print`.
// Licensed under the Apache License 2.0 (see LICENSE).
(function (write) {
  "use strict";

  // console: a compact subset of Node's formatting (one line per value, depth 2).
  function quote(s) {
    // Node's choice: single quotes, else double, else backticks, whichever the string doesn't contain.
    var q = s.indexOf("'") < 0 ? "'" : s.indexOf('"') < 0 ? '"' : s.indexOf("`") < 0 ? "`" : "'";
    s = s.replace(/\\/g, "\\\\").replace(/\n/g, "\\n");
    return q + (q === "'" ? s.replace(/'/g, "\\'") : s) + q;
  }
  function key(k) {
    return typeof k === "symbol" ? "[" + k.toString() + "]" : /^[A-Za-z_$][\w$]*$/.test(k) ? k : quote(k);
  }
  function inspect(v, depth, seen) {
    switch (typeof v) {
      case "string": return depth === 0 ? v : quote(v);
      case "number": return Object.is(v, -0) ? "-0" : String(v);
      case "bigint": return v + "n";
      case "undefined": return "undefined";
      case "boolean": return String(v);
      case "symbol": return v.toString();
      case "function": {
        // Hermes keeps no source text; a class's `prototype` is the one that isn't writable.
        var p = Object.getOwnPropertyDescriptor(v, "prototype");
        if (p && !p.writable) return "[class " + (v.name || "(anonymous)") + "]";
        return "[Function" + (v.name ? ": " + v.name : " (anonymous)") + "]";
      }
    }
    if (v === null) return "null";
    if (seen.indexOf(v) >= 0) return "[Circular]";
    if (v instanceof Error) return v.stack || String(v);
    if (v instanceof Date) return isNaN(v) ? "Invalid Date" : v.toISOString();
    if (v instanceof RegExp) return String(v);
    if (depth > 2) return Array.isArray(v) ? "[Array]" : "[Object]";
    seen = seen.concat([v]);
    var inner = function (x) { return inspect(x, depth + 1, seen); };
    var list = function (prefix, items, open, close) {
      return (prefix ? prefix + " " : "") + (items.length ? open + " " + items.join(", ") + " " + close : open + close);
    };
    if (Array.isArray(v)) return list("", v.map(inner), "[", "]");
    if (ArrayBuffer.isView(v) && !(v instanceof DataView)) {
      return list(v.constructor.name + "(" + v.length + ")", Array.prototype.map.call(v, inner), "[", "]");
    }
    if (v instanceof ArrayBuffer) return "ArrayBuffer { byteLength: " + v.byteLength + " }";
    if (v instanceof Map) {
      var m = [];
      v.forEach(function (val, k) { m.push(inner(k) + " => " + inner(val)); });
      return list("Map(" + v.size + ")", m, "{", "}");
    }
    if (v instanceof Set) {
      var s = [];
      v.forEach(function (val) { s.push(inner(val)); });
      return list("Set(" + v.size + ")", s, "{", "}");
    }
    if (v instanceof Promise) return "Promise {}";
    var proto = Object.getPrototypeOf(v);
    var name = proto === null ? "[Object: null prototype]" : proto !== Object.prototype && proto.constructor ? proto.constructor.name : "";
    var props = Reflect.ownKeys(v).filter(function (k) { return Object.prototype.propertyIsEnumerable.call(v, k); });
    return list(name, props.map(function (k) { return key(k) + ": " + inner(v[k]); }), "{", "}");
  }
  function format(args) {
    var out = [];
    var i = 0;
    if (typeof args[0] === "string" && args.length > 1) {
      i = 1;
      out.push(args[0].replace(/%[sdifjoO%]/g, function (spec) {
        if (spec === "%%") return "%";
        if (i >= args.length) return spec;
        var a = args[i++];
        switch (spec) {
          case "%s": return typeof a === "string" ? a : inspect(a, 1, []);
          case "%d": case "%i": return typeof a === "bigint" ? a + "n" : String(spec === "%i" ? parseInt(a) : Number(a));
          case "%f": return String(parseFloat(a));
          case "%j": try { return JSON.stringify(a); } catch (e) { return "[Circular]"; }
          default: return inspect(a, 1, []);
        }
      }));
    }
    for (; i < args.length; i++) out.push(inspect(args[i], 0, []));
    return out.join(" ") + "\n";
  }
  var counts = {};
  var timers = {};
  var console = {
    log: function (...a) { write(1, format(a)); },
    info: function (...a) { write(1, format(a)); },
    debug: function (...a) { write(1, format(a)); },
    warn: function (...a) { write(2, format(a)); },
    error: function (...a) { write(2, format(a)); },
    trace: function (...a) { write(2, format(["Trace:"].concat(a)) + (new Error().stack || "").split("\n").slice(2).join("\n") + "\n"); },
    assert: function (ok, ...a) { if (!ok) write(2, format(["Assertion failed" + (a.length ? ":" : "")].concat(a))); },
    count: function (label) { label = label === undefined ? "default" : String(label); counts[label] = (counts[label] || 0) + 1; write(1, label + ": " + counts[label] + "\n"); },
    time: function (label) { timers[label === undefined ? "default" : String(label)] = Date.now(); },
    timeEnd: function (label) {
      label = label === undefined ? "default" : String(label);
      if (label in timers) { write(1, label + ": " + (Date.now() - timers[label]) + "ms\n"); delete timers[label]; }
    },
  };
  globalThis.console = console;
  globalThis.print = globalThis.print || function (...a) { write(1, a.join(" ") + "\n"); };

});
