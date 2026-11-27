// hermes-jsi-run demo.js libjsirunworker.so
console.log("add(2, 3) =", add(2, 3));
(async () => {
  const [a, b] = await Promise.all([double(1), double(2)]);
  console.log("awaited", { a, b }, [a + b], new Uint8Array([a, b]));
  console.log("double(21) resolved on another thread:", await double(21));
  setTimeout(() => console.log("timer after 50 ms"), 50);
})();
queueMicrotask(() => console.log("microtask before any timer"));
console.log("script done; the loop keeps running while work is pending");
