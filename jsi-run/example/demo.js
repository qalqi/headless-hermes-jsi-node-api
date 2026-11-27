// hermes-jsi-run demo.js libjsirunworker.so
console.log("add(2, 3) =", add(2, 3));
setTimeout(() => console.log("timer after 50 ms"), 50);
double(21).then((v) => console.log("double(21) resolved on another thread:", v));
(async () => {
  const [a, b] = await Promise.all([double(1), double(2)]);
  console.log("awaited", { a, b }, [a + b], new Uint8Array([a, b]));
})();
queueMicrotask(() => console.log("microtask before any timer"));
console.log("script done; the loop keeps running while work is pending");
