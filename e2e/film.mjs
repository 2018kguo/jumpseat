// Records the README's film: the demo viewer in real Chrome while a scripted
// "agent" drives the desktop through the API, then hands a step to the
// person, who takes over and hands it back.
//   docker run -d --name jumpseat-film -p 18090:8080 -e JUMPSEAT_TOKEN=secret -e JUMPSEAT_RESIZE=1 jumpseat
//   JUMPSEAT_URL=http://localhost:18090 JUMPSEAT_CONTAINER=jumpseat-film node film.mjs out-dir
// Writes Chrome's screencast frames and out-dir/frames.txt, then:
//   ffmpeg -f concat -i out-dir/frames.txt -r 30 -c:v libx264 -crf 20 -pix_fmt yuv420p film.mp4
//   ffmpeg -i film.mp4 -vf "fps=15,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" docs/film.gif
import { execFileSync } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";

import { chromium } from "playwright";

const base = process.env.JUMPSEAT_URL ?? "http://localhost:18090";
const token = process.env.JUMPSEAT_TOKEN ?? "secret";
const container = process.env.JUMPSEAT_CONTAINER ?? "jumpseat-film";
const out = process.argv[2] ?? "film";
const api = (path, body) =>
  fetch(`${base}${path}`, {
    method: body ? "POST" : "GET",
    headers: { authorization: `Bearer ${token}`, "content-type": "application/json" },
    body: body ? JSON.stringify(body) : undefined,
  });
const x = (...args) => execFileSync("docker", ["exec", "-e", "DISPLAY=:0", container, "xdotool", ...args]);
const say = (kind, text) => api("/agent/status", { kind, text });
const settle = () => api("/agent/wait?idle=400&timeout=8000");
const pause = (ms) => new Promise((done) => setTimeout(done, ms));

// The window, not an emulated viewport, sets the page's size: headless
// Chrome keeps 88 px of its window the page never gets, and an emulated
// viewport's bottom 88 px went missing from the capture.
const browser = await chromium.launch({ channel: "chrome", args: ["--window-size=1280,940"] });
const context = await browser.newContext({ viewport: null });
const page = await context.newPage();
mkdirSync(out, { recursive: true });
const frames = [];
const cdp = await context.newCDPSession(page);
cdp.on("Page.screencastFrame", ({ data, metadata, sessionId }) => {
  const file = `${String(frames.length).padStart(5, "0")}.jpg`;
  writeFileSync(`${out}/${file}`, Buffer.from(data, "base64"));
  frames.push({ file, at: metadata.timestamp });
  cdp.send("Page.screencastFrameAck", { sessionId }).catch(() => {});
});
// The recording has no cursor of its own: draw the person's.
await page.addInitScript(() => {
  addEventListener("DOMContentLoaded", () => {
    const dot = document.createElement("div");
    dot.style.cssText =
      "position:fixed;z-index:9;width:18px;height:18px;margin:-9px 0 0 -9px;border-radius:50%;background:rgba(255,255,255,.9);box-shadow:0 0 0 2px #111;pointer-events:none;display:none";
    document.body.append(dot);
    addEventListener("pointermove", (e) => {
      dot.style.display = "block";
      dot.style.left = `${e.clientX}px`;
      dot.style.top = `${e.clientY}px`;
    });
  });
});

await say("idle", "");
const chromium_ = () => String(x("search", "--onlyvisible", "--class", "chromium")).trim().split("\n")[0];
const win = chromium_();
x("windowminimize", win);
await page.goto(`${base}/?token=${token}&resize`);
await page.getByText("Agent in control").waitFor({ timeout: 30000 });
await settle();
await cdp.send("Page.startScreencast", { format: "jpeg", quality: 92, maxWidth: 1280, maxHeight: 852 });
await pause(1800);

// The agent works; its captions show, and it waits for each page to settle.
await say("acting", "Opening the browser");
await pause(900);
x("windowactivate", win);
await settle();
await say("acting", "Opening Wikipedia");
x("key", "ctrl+l", "type", "--delay", "40", "en.wikipedia.org/wiki/Jump_seat");
x("key", "Return");
await settle();
await pause(1500);
await say("acting", "Reading the article");
for (let i = 0; i < 6; i++) {
  x("click", "5");
  await pause(120);
}
await settle();
await pause(1200);

// A step only a person can do.
await say("handoff", "Pick the section to summarize");
await pause(2200);
await page.getByRole("button", { name: "Take over" }).click();
await page.mouse.move(700, 450, { steps: 20 });
for (let i = 0; i < 40; i++) {
  await page.mouse.wheel(0, -12);
  await pause(16);
}
await pause(500);
for (let i = 0; i < 60; i++) {
  await page.mouse.wheel(0, 9);
  await pause(16);
}
await pause(900);
// The page's own Contents list: "In aviation".
await page.mouse.move(107, 264, { steps: 25 });
await pause(300);
await page.mouse.click(107, 264);
await settle();
await pause(1200);
await page.getByRole("button", { name: "Done, continue" }).click();
await pause(400);

await say("acting", "Summarizing \u201cIn aviation\u201d");
await pause(2000);

// With --resize the desktop follows the window.
const { windowId } = await cdp.send("Browser.getWindowForTarget");
const windowWidth = (width) => cdp.send("Browser.setWindowBounds", { windowId, bounds: { width } });
await windowWidth(1000);
await pause(1800);
await windowWidth(1280);
await pause(1800);
await say("idle", "");
await pause(800);

await cdp.send("Page.stopScreencast");
// Each frame lasts until the next one: the screencast sends only changes.
const list = ["ffconcat version 1.0"];
frames.forEach((frame, i) => {
  const next = frames[i + 1]?.at ?? frame.at + 0.5;
  list.push(`file ${frame.file}`, `duration ${(next - frame.at).toFixed(4)}`);
});
list.push(`file ${frames.at(-1).file}`);
writeFileSync(`${out}/frames.txt`, `${list.join("\n")}\n`);
console.log(`${frames.length} frames, ${(frames.at(-1).at - frames[0].at).toFixed(1)} s`);
await context.close();
await browser.close();
