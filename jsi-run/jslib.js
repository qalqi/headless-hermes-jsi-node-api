// The JavaScript side of hermes-jsi-run's event loop: timers, installed before the script runs (console is
// common/console.js). Returns the timer queue's `peek()` and `run(now)` for the event loop.
// Licensed under the Apache License 2.0 (see LICENSE).
(function () {
  "use strict";

  // Timers: a list sorted by deadline, then by creation (ties run in the order they were set).
  var tasks = [];
  var nextId = 1;
  var now = 0;
  function peek() {
    return tasks.length ? tasks[0].deadline : -1;
  }
  function run(tm) {
    now = tm;
    if (tasks.length && tasks[0].deadline <= tm) {
      var task = tasks.shift();
      if (task.interval !== undefined) insert(task, now + task.interval);
      task.fn.apply(undefined, task.args);
    }
  }
  function insert(task, deadline) {
    task.deadline = deadline;
    var i = 0;
    while (i < tasks.length && tasks[i].deadline <= deadline) i++;
    tasks.splice(i, 0, task);
  }
  function setTimer(fn, ms, args, repeat) {
    if (typeof fn !== "function") throw new TypeError("The callback must be a function");
    var delay = Math.max(0, Number(ms) || 0);
    var task = { id: nextId++, fn: fn, args: args, interval: repeat ? Math.max(1, delay) : undefined };
    insert(task, now + delay);
    return task.id;
  }
  function clearTimer(id) {
    for (var i = 0; i < tasks.length; i++) {
      if (tasks[i].id === id) {
        tasks.splice(i, 1);
        return;
      }
    }
  }
  globalThis.setTimeout = function (fn, ms, ...args) { return setTimer(fn, ms, args, false); };
  globalThis.setInterval = function (fn, ms, ...args) { return setTimer(fn, ms, args, true); };
  globalThis.setImmediate = function (fn, ...args) { return setTimer(fn, 0, args, false); };
  globalThis.clearTimeout = globalThis.clearInterval = globalThis.clearImmediate = clearTimer;
  if (typeof globalThis.queueMicrotask !== "function") {
    globalThis.queueMicrotask = function (fn) { Promise.resolve().then(fn); };
  }

  return { peek: peek, run: run };
});
