// What a person watching sees, jumpseat against noVNC, on lab.py's sandbox:
// a real Chrome opens each viewer across the network and measures, per
// workload (bench/compare/page.html):
//   fps    distinct pictures the viewer painted per second
//   kbps   websocket bytes received
//   cores  CPU of the X server plus the streaming server, on the sandbox
//   key    key press in the viewer -> the page's marker flipping in the viewer
// Servers alternate ABBA on the one sandbox (two machines differ by more).
//   node bench/compare/probe.mjs [seconds=10] [rounds=2]
import { execFileSync } from "node:child_process";

import { chromium } from "playwright";

const here = new URL(".", import.meta.url).pathname;
const lab = (...args) => execFileSync("python3", [`${here}lab.py`, ...args], { encoding: "utf8" }).trim();
const [url, token] = lab().split(" ");
const seconds = Number(process.argv[2] ?? 10);
const rounds = Number(process.argv[3] ?? 2);
const OWN = { jumpseat: ["jumpseat"], novnc: ["x11vnc", "websockify"] };

const ticks = (names) => {
  let sum = 0;
  for (const line of lab("cpu").split("\n")) {
    const at = line.lastIndexOf(" ");
    if (["Xvfb", ...names].includes(line.slice(0, at))) sum += Number(line.slice(at + 1));
  }
  return sum;
};

const browser = await chromium.launch({ channel: process.env.PROBE_BROWSER ?? "chrome" });

async function open(server) {
  const page = await browser.newPage({ viewport: { width: 1300, height: 900 } });
  const cdp = await page.context().newCDPSession(page);
  await cdp.send("Network.enable");
  let bytes = 0;
  cdp.on("Network.webSocketFrameReceived", ({ response }) => {
    bytes += response.opcode === 2 ? Math.floor((response.payloadData.length * 3) / 4) : response.payloadData.length;
  });
  if (server === "jumpseat") {
    await page.goto(`${url}/?token=${token}`);
    await page.getByText("Agent in control").waitFor({ timeout: 30000 });
    await page.getByRole("button", { name: "Take over" }).click();
  } else {
    await page.goto(`${url}/vnc_lite.html?autoconnect=1&resize=off`);
    await page.waitForFunction(() => document.querySelector("canvas")?.width > 100, null, { timeout: 30000 });
  }
  await page.waitForTimeout(2000);
  // The remote screen is the largest canvas; sample it every animation frame.
  await page.evaluate(() => {
    const canvas = [...document.querySelectorAll("canvas")].sort((a, b) => b.width * b.height - a.width * a.height)[0];
    const small = new OffscreenCanvas(48, 30).getContext("2d", { willReadFrequently: true });
    const dot = new OffscreenCanvas(1, 1).getContext("2d", { willReadFrequently: true });
    // The page's marker: the first black 40 px square down the left edge.
    const full = new OffscreenCanvas(canvas.width, canvas.height).getContext("2d", { willReadFrequently: true });
    full.drawImage(canvas, 0, 0);
    let markerY = -1;
    for (let y = 0; y < 300 && markerY < 0; y++) {
      const [r] = full.getImageData(20, y, 1, 1).data, [r2] = full.getImageData(20, y + 30, 1, 1).data;
      if (r < 30 && r2 < 30) markerY = y + 20;
    }
    const s = (window.probe = { frames: 0, last: "", markerY, lastKey: 0, keyMs: [], shade: -1 });
    addEventListener("keydown", () => { s.lastKey = performance.now(); }, true);
    (function tick() {
      small.drawImage(canvas, 0, 0, 48, 30);
      const sig = small.getImageData(0, 0, 48, 30).data.join();
      if (sig !== s.last) { s.frames++; s.last = sig; }
      if (markerY >= 0) {
        dot.drawImage(canvas, 20, markerY, 1, 1, 0, 0, 1, 1);
        const shade = dot.getImageData(0, 0, 1, 1).data[0] > 128 ? 1 : 0;
        if (s.shade >= 0 && shade !== s.shade && s.lastKey) { s.keyMs.push(performance.now() - s.lastKey); s.lastKey = 0; }
        s.shade = shade;
      }
      requestAnimationFrame(tick);
    })();
    canvas.focus?.();
  });
  await page.mouse.click(700, 500);
  return { page, bytes: () => bytes };
}

async function measure(server, workload) {
  lab("page", workload);
  const viewer = await open(server);
  await viewer.page.waitForTimeout(1500);
  const f0 = await viewer.page.evaluate(() => window.probe.frames), b0 = viewer.bytes(), c0 = ticks(OWN[server]);
  const started = Date.now();
  // Twenty key presses, 400 ms apart, unless the window is shorter.
  for (let i = 0; i < 20 && Date.now() - started < seconds * 1000 - 500; i++) {
    await viewer.page.keyboard.press("Shift");
    await viewer.page.waitForTimeout(400);
  }
  await viewer.page.waitForTimeout(Math.max(0, seconds * 1000 - (Date.now() - started)));
  const elapsed = (Date.now() - started) / 1000;
  const probe = await viewer.page.evaluate(() => window.probe);
  const result = {
    server, workload,
    fps: (probe.frames - f0) / elapsed,
    kbps: ((viewer.bytes() - b0) * 8) / 1000 / elapsed,
    cores: (ticks(OWN[server]) - c0) / 100 / elapsed,
    keyP50: probe.keyMs.sort((a, b) => a - b)[Math.floor(probe.keyMs.length / 2)] ?? null,
    keys: probe.keyMs.length,
    marker: probe.markerY >= 0,
  };
  await viewer.page.close();
  console.log(JSON.stringify(result));
  return result;
}

const results = [];
for (let r = 0; r < rounds; r++) {
  for (const server of r % 2 ? ["novnc", "jumpseat"] : ["jumpseat", "novnc"]) {
    lab("serve", server);
    for (const workload of ["type", "scroll", "anim"]) results.push(await measure(server, workload));
  }
}
await browser.close();
const median = (xs) => xs.sort((a, b) => a - b)[Math.floor(xs.length / 2)];
console.log("\nmedians");
for (const workload of ["type", "scroll", "anim"]) {
  for (const server of ["jumpseat", "novnc"]) {
    const rows = results.filter((x) => x.server === server && x.workload === workload);
    const m = (k) => median(rows.map((x) => x[k]).filter((v) => v !== null));
    console.log(`${workload.padEnd(6)} ${server.padEnd(8)} ${m("fps").toFixed(1)} fps  ${(m("kbps") / 1000).toFixed(2)} Mbit/s  ${m("cores").toFixed(2)} cores  key p50 ${m("keyP50")?.toFixed(0)} ms`);
  }
}
