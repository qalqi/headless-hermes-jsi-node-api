// napi-host require-demo/main.js <addon.node> ... -- <path to hello.node>
// CommonJS as in Node: relative files, JSON, a package from node_modules, and a Node-API addon through require().
const { square } = require("./lib/math");
const config = require("./lib/config.json");
const greet = require("greeting");
const hello = require(process.argv[2]);

console.log("square(7) =", square(7));
console.log("config:", config);
console.log(greet("Hermes"));
console.log("addon via require:", hello.hello(), hello.add(2, 3));
console.log("same module object twice:", require("./lib/math") === require("./lib/math.js"));
console.log("platform known:", ["linux", "darwin"].includes(process.platform), typeof __filename, typeof __dirname);
try {
  require("./missing");
} catch (e) {
  console.log(e.code);
}
