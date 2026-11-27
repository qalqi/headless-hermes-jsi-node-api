// console in hermes-jsi-run prints what Node prints for these (expected/jsi-run-console.txt is Node's output).
console.log("%s is %d years", "Ada", 36, "extra");
console.log([1, "two", [3, [4, [5]]]], { a: { b: { c: { d: 1 } } } }, null, undefined, -0, 10n);
console.log(new Map([["k", 1]]), new Set([1, 2]), function named() {}, class K {}, Symbol("s"));
console.log({ "x-y": 1, z: "q'q" }, [], {}, new Float64Array([1.5]));
console.log("back\\\\slash", ["a\\nb"], new Error("an error message").message);
