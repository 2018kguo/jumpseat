// How sharp the viewer's picture is once the screen goes still: animate a
// page of text for 2 s, stop, wait, then compare the viewer's canvas with
// /agent/screenshot (the lossless screen). Prints PSNR (luma) at several
// times after the animation stops.
//   JUMPSEAT_URL=http://localhost:8080 JUMPSEAT_CONTAINER=jumpseat node fidelity.mjs
import { execFileSync } from "node:child_process";

import { chromium } from "playwright";

const base = process.env.JUMPSEAT_URL ?? "http://localhost:18080";
const token = process.env.JUMPSEAT_TOKEN ?? "secret";
const container = process.env.JUMPSEAT_CONTAINER ?? "jumpseat-test";
const docker = (process.env.JUMPSEAT_DOCKER ?? "docker").split(" ");
const xdo = (...args) => execFileSync(docker[0], [...docker.slice(1), "exec", "-e", "DISPLAY=:0", container, "xdotool", ...args]);

const text = "The quick brown fox jumps over the lazy dog. 0123456789 ".repeat(400);
// The page animates for 2.5 s, then stops on its own: FREEZE=1 leaves the
// last animated frame on screen, otherwise the animation goes away.
const html = `<body style="margin:20px;font:14px sans-serif;color:#223;background:#fdfcf8">
<div id=a style="position:fixed;inset:0;background:conic-gradient(red,yellow,lime,cyan,blue,magenta,red);opacity:.6"></div>
<p>${text}</p><script>let t=0,run=true;const a=document.getElementById('a');
setTimeout(()=>{run=false;${process.env.FREEZE ? "" : "a.remove();"}document.title='stopped'},2500);
const f=()=>{if(!run)return;a.style.transform='rotate('+(t+=3)+'deg) scale(2)';requestAnimationFrame(f)};f()</script>`;
execFileSync(docker[0], [...docker.slice(1), "exec", "-i", container, "sh", "-c", "cat > /tmp/fidelity.html"], { input: html });

const browser = await chromium.launch({ channel: process.env.JUMPSEAT_BROWSER ?? "chrome" });
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
await page.goto(`${base}/?token=${token}`);
await page.getByText("Agent in control").waitFor({ timeout: 30000 });

xdo("mousemove", "1270", "790"); // the pointer out of the way
xdo("key", "ctrl+l");
await page.waitForTimeout(300);
xdo("type", "file:///tmp/fidelity.html");
xdo("key", "Return");
const stopped = Date.now() + 2500;

await page.waitForTimeout(Math.max(0, stopped + 50 - Date.now()));
const shot = Buffer.from(await (await fetch(`${base}/agent/screenshot`, { headers: { authorization: `Bearer ${token}` } })).arrayBuffer()).toString("base64");
for (const at of [100, 300, 1000, 3000]) {
  const wait = stopped + at - Date.now();
  if (wait > 0) await page.waitForTimeout(wait);
  const db = await page.evaluate(async (png) => {
    const canvas = document.querySelector("canvas");
    const w = canvas.width, h = canvas.height;
    const read = (src) => {
      const c = new OffscreenCanvas(w, h);
      const x = c.getContext("2d");
      x.drawImage(src, 0, 0, w, h);
      return x.getImageData(0, 0, w, h).data;
    };
    const ref = read(await createImageBitmap(await (await fetch(`data:image/png;base64,${png}`)).blob()));
    const got = read(canvas);
    let se = 0;
    const y = (d, i) => 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
    for (let i = 0; i < ref.length; i += 4) se += (y(ref, i) - y(got, i)) ** 2;
    return 10 * Math.log10(255 ** 2 / (se / (ref.length / 4)));
  }, shot);
  console.log(`${at} ms after the screen settles: ${db.toFixed(2)} dB`);
}
await browser.close();
