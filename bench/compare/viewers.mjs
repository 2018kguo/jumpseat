// Two viewers on one jumpseat (lab.py's sandbox): does a slow viewer slow a
// fast one down? Viewer A watches over this machine's own link; viewer B's
// connection goes through a shaping proxy in this process (below).
// Per workload: A alone, B alone, both (A presses the keys). Each measures:
//   fps    distinct pictures painted per second
//   kbps   websocket bytes received
//   key    A's key press -> the page's marker flipping in that viewer, p50
//   cores  CPU of Xvfb and jumpseat on the sandbox
//   node bench/compare/viewers.mjs [seconds=10] [rounds=2] [kbps=1000] [delay=40]
import { execFileSync } from "node:child_process";
import net from "node:net";
import tls from "node:tls";

import { chromium } from "playwright";

const here = new URL(".", import.meta.url).pathname;
const lab = (...args) => execFileSync("python3", [`${here}lab.py`, ...args], { encoding: "utf8" }).trim();
const [url, token] = lab().split(" ");
const [seconds, rounds, kbps, delay] = [10, 2, 1000, 40].map((d, i) => Number(process.argv[2 + i] ?? d));

const ticks = () => {
  const out = {};
  for (const line of lab("cpu").split("\n")) {
    const at = line.lastIndexOf(" ");
    const name = line.slice(0, at);
    if (name === "Xvfb" || name === "jumpseat") out[name] = (out[name] ?? 0) + Number(line.slice(at + 1));
  }
  return out;
};

// Viewer B's link: a TCP proxy on this machine to the sandbox's TLS port,
// server to viewer at kbps behind a 256 KB buffer (a router's or a socket's:
// once full, TCP pushes back on the server), DELAY ms each way.
let shapedBytes = 0;
const host = new URL(url).host;
// A one-way link: chunks leave in order, each `delay` ms after the link has
// serialised it at `rate` kbit/s (Infinity: no limit). Over `buffer` bytes
// queued, the source stops being read, as a full socket buffer stops a sender.
function link(from, to, rate, buffer, onSent = () => {}) {
  const line = [];
  let free = 0, queued = 0, timer = null;
  const pump = () => {
    timer = null;
    while (line.length && line[0].at <= Date.now()) {
      const { chunk } = line.shift();
      to.write(chunk);
      onSent(chunk.length);
      queued -= chunk.length;
    }
    if (queued < buffer) from.resume();
    if (line.length) timer = setTimeout(pump, line[0].at - Date.now());
  };
  return (chunk) => {
    free = Math.max(free, Date.now()) + (chunk.length * 8) / rate;
    line.push({ chunk, at: free + delay });
    queued += chunk.length;
    if (queued >= buffer) from.pause();
    if (!timer) timer = setTimeout(pump, line[0].at - Date.now());
  };
}

const proxy = net.createServer((client) => {
  const upstream = tls.connect({ host, port: 443, servername: host });
  const down = link(upstream, client, kbps, 256 * 1024, (n) => { shapedBytes += n; });
  const up = link(client, upstream, Infinity, Infinity);
  upstream.on("data", down);
  // The sandbox's tunnel routes by Host; the page's own origin is this proxy.
  client.on("data", (chunk) => up(Buffer.from(chunk.toString("latin1").replaceAll(`127.0.0.1:${proxy.address().port}`, host), "latin1")));
  const end = () => { client.destroy(); upstream.destroy(); };
  client.on("error", end).on("close", end);
  upstream.on("error", end).on("close", end);
}).listen(0, "127.0.0.1");
await new Promise((r) => proxy.on("listening", r));

async function open(browser, { drive, shaped }) {
  const page = await browser.newPage({ viewport: { width: 1300, height: 900 } });
  let bytes = 0;
  page.on("websocket", (ws) => ws.on("framereceived", ({ payload }) => { bytes += payload.length; }));
  await page.goto(`${shaped ? `http://127.0.0.1:${proxy.address().port}` : url}/?token=${token}`);
  await page.getByText(/in control/).waitFor({ timeout: 30000 }).catch(async (e) => {
    throw new Error(`${shaped ? "B" : "A"} never connected: ${(await page.innerText("body")).slice(0, 300)}`, { cause: e });
  });
  if (drive) await page.getByRole("button", { name: "Take over" }).click();
  await page.waitForTimeout(2000);
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
    const s = (window.probe = { frames: 0, last: "", flips: [], shade: -1, markerY });
    (function tick() {
      small.drawImage(canvas, 0, 0, 48, 30);
      const sig = small.getImageData(0, 0, 48, 30).data.join();
      if (sig !== s.last) { s.frames++; s.last = sig; }
      dot.drawImage(canvas, 20, markerY, 1, 1, 0, 0, 1, 1);
      const shade = dot.getImageData(0, 0, 1, 1).data[0] > 128 ? 1 : 0;
      if (s.shade >= 0 && shade !== s.shade) s.flips.push(Date.now());
      s.shade = shade;
      requestAnimationFrame(tick);
    })();
  });
  if (drive) await page.mouse.click(700, 500);
  return { page, bytes: () => (shaped ? shapedBytes : bytes) };
}

// Each key press's delay to the first flip after it, in one viewer.
const keyDelays = (presses, flips) =>
  presses.map((t) => flips.find((f) => f >= t)).map((f, i) => (f === undefined ? null : f - presses[i])).filter((d) => d !== null && d < 10000);
const p50 = (xs) => (xs.length ? xs.sort((a, b) => a - b)[Math.floor(xs.length / 2)] : null);

// setup: "A" (fast alone), "B" (slow alone) or "AB" (both; A drives).
async function measure(workload, setup) {
  lab("page", workload);
  const viewers = [];
  for (const name of setup) {
    const browser = await chromium.launch({ channel: "chrome" });
    viewers.push({ name, browser, ...(await open(browser, { drive: !viewers.length, shaped: name === "B" })) });
  }
  const driver = viewers[0].page;
  await driver.waitForTimeout(2000);
  const f0 = await Promise.all(viewers.map((v) => v.page.evaluate(() => window.probe.frames)));
  const b0 = viewers.map((v) => v.bytes());
  const c0 = ticks();
  const started = Date.now();
  const presses = [];
  for (let i = 0; i < 20 && Date.now() - started < seconds * 1000 - 500; i++) {
    presses.push(Date.now());
    await driver.keyboard.press("Shift");
    await driver.waitForTimeout(400);
  }
  await driver.waitForTimeout(Math.max(0, seconds * 1000 - (Date.now() - started)));
  const elapsed = (Date.now() - started) / 1000;
  const c1 = ticks();
  const rows = [];
  for (const [i, v] of viewers.entries()) {
    const probe = await v.page.evaluate(() => window.probe);
    const keys = keyDelays(presses, probe.flips);
    rows.push({
      workload, setup, viewer: v.name,
      fps: (probe.frames - f0[i]) / elapsed,
      kbps: ((v.bytes() - b0[i]) * 8) / 1000 / elapsed,
      key: p50(keys), keys: `${keys.length}/${presses.length}`,
      xvfb: ((c1.Xvfb ?? 0) - (c0.Xvfb ?? 0)) / 100 / elapsed,
      jumpseat: ((c1.jumpseat ?? 0) - (c0.jumpseat ?? 0)) / 100 / elapsed,
    });
  }
  for (const v of viewers) await v.browser.close();
  for (const r of rows) console.log(JSON.stringify(r));
  return rows;
}

lab("serve", "jumpseat");
const SETUPS = (process.env.SETUPS ?? "A,B,AB").split(",");
const WORKLOADS = (process.env.WORKLOADS ?? "type,scroll,anim").split(",");
const results = [];
for (let r = 0; r < rounds; r++) {
  for (const workload of WORKLOADS) {
    for (const setup of r % 2 ? [...SETUPS].reverse() : SETUPS) results.push(...(await measure(workload, setup)));
  }
}
const median = (xs) => (xs.length ? xs.sort((a, b) => a - b)[Math.floor(xs.length / 2)] : null);
console.log(`\nmedians (B: ${kbps} kbit/s, +${delay} ms each way)`);
for (const workload of WORKLOADS) {
  for (const setup of SETUPS) {
    for (const viewer of setup) {
      const rows = results.filter((x) => x.workload === workload && x.setup === setup && x.viewer === viewer);
      const m = (k) => median(rows.map((x) => x[k]).filter((v) => v !== null));
      console.log(`${workload.padEnd(6)} ${setup.padEnd(2)} ${viewer}  ${m("fps")?.toFixed(1)} fps  ${(m("kbps") / 1000).toFixed(2)} Mbit/s  key p50 ${m("key")?.toFixed(0) ?? "-"} ms (${rows.map((x) => x.keys).join(" ")})  cores Xvfb ${m("xvfb")?.toFixed(2)} jumpseat ${m("jumpseat")?.toFixed(2)}`);
    }
  }
}
proxy.close();
