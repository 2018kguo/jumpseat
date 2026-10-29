// A headless viewer for bench/wifi: speaks jumpseat's websocket protocol
// (docs/reference.md), acks every frame as a viewer that shows it at once,
// and measures what a person at the keyboard feels:
//   type   key press -> first frame after it (the typed character)
//   wheel  wheel step -> first frame after it (the page scrolled)
//   anim   frames/s and the gaps between frames while the page animates
// Usage: node probe.mjs URL TOKEN [label]  -> one JSON line
const [url, token, label = ""] = process.argv.slice(2);
const ws = new WebSocket(url.replace(/^http/, "ws") + "/stream?token=" + token);
ws.binaryType = "arraybuffer";
const arrivals = []; // [t, bytes, key]
let firstFrame;
const opened = new Promise((r, j) => { ws.onopen = r; ws.onerror = () => j(new Error("socket error")); });
ws.onmessage = (e) => {
  if (typeof e.data === "string") return;
  const b = new Uint8Array(e.data), t = performance.now();
  const id = new DataView(e.data).getBigUint64(1);
  arrivals.push([t, b.length, b[0]]);
  if (!firstFrame) firstFrame = t;
  ws.send(JSON.stringify({ t: "a", ts: Number(id) }));
};
const send = (m) => ws.send(JSON.stringify(m));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const quantile = (xs, q) => { const s = [...xs].sort((a, b) => a - b); return s.length ? Math.round(s[Math.min(s.length - 1, Math.floor(q * s.length))]) : null; };
// Wait until the stream has been quiet for `ms` (bounded), so the next input's frame is its own.
async function quiet(ms, limit = 3000) {
  const start = performance.now();
  while (performance.now() - start < limit) {
    const last = arrivals.length ? arrivals[arrivals.length - 1][0] : 0;
    if (performance.now() - last >= ms) return;
    await sleep(10);
  }
}
async function response(act, n, gap) {
  const lat = [];
  for (let i = 0; i < n; i++) {
    await quiet(gap);
    const t0 = performance.now(), seen = arrivals.length;
    act(i);
    while (arrivals.length === seen && performance.now() - t0 < 3000) await sleep(1);
    lat.push(arrivals.length > seen ? arrivals[seen][0] - t0 : 3000);
  }
  return lat;
}
const t0 = performance.now();
await opened;
const heartbeat = setInterval(() => send({ t: "h" }), 5000);
while (!firstFrame && performance.now() - t0 < 15000) await sleep(5);
const connect = Math.round(firstFrame - t0);
await quiet(400, 8000);
send({ t: "b", b: 1, down: true, x: 400, y: 80 }); send({ t: "b", b: 1, down: false, x: 400, y: 80 }); // focus the textarea
await quiet(300);
const keys = "the quick brown fox jumps over the lazy dog 0123456789".split("");
const nkeys = Number(process.env.KEYS || 50);
const type = await response((i) => { send({ t: "k", key: keys[i % keys.length], down: true }); send({ t: "k", key: keys[i % keys.length], down: false }); }, nkeys, 250);
send({ t: "m", x: 640, y: 600 });
const wheel = await response((i) => send({ t: "w", dx: 0, dy: i % 10 < 5 ? 1 : -1 }), 20, 300);
await quiet(300);
send({ t: "k", key: "F2", down: true }); send({ t: "k", key: "F2", down: false });
await sleep(500);
const a0 = arrivals.length, s0 = performance.now();
await sleep(6000);
const anim = arrivals.slice(a0).map((a) => a[0]);
send({ t: "k", key: "F3", down: true }); send({ t: "k", key: "F3", down: false });
await sleep(200);
const gaps = anim.slice(1).map((t, i) => t - anim[i]);
const kbps = Math.round(arrivals.slice(a0).reduce((s, a) => s + a[1], 0) * 8 / 6000);
clearInterval(heartbeat);
ws.close();
const summary = (xs) => ({ p50: quantile(xs, 0.5), p90: quantile(xs, 0.9), p95: quantile(xs, 0.95), p99: quantile(xs, 0.99), max: quantile(xs, 1), all: xs.map(Math.round) });
console.log(JSON.stringify({ label, connect, type: summary(type), wheel: summary(wheel),
  anim: { fps: Math.round(anim.length / 6 * 10) / 10, kbps, gap: summary(gaps), stalls200: gaps.filter((g) => g > 200).length } }));
process.exit(0);
