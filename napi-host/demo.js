print("start");

let cancelled = false;
const id = setTimeout(() => {
  cancelled = true;
  print("should not print");
}, 10);
clearTimeout(id);

Promise.resolve("microtask").then((v) => {
  print(v);
});

setTimeout((msg) => {
  print("timer", msg, "cancelled=" + cancelled);
}, 20, "done");

print("end");
