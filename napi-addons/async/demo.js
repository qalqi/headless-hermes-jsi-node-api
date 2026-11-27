async function main() {
  print("1. calling asyncAdd(19, 23)");
  const sum = await async_addon.asyncAdd(19, 23);
  print("2. asyncAdd resolved:", sum);

  const ticks = await new Promise((resolve) => {
    const received = [];
    async_addon.emitFromThread(4, (val) => {
      received.push(val);
      if (received.length === 4) {
        resolve(received);
      }
    });
  });
  print("3. thread ticks:", ticks.join(","));
}

main().then(() => print("4. done"));
