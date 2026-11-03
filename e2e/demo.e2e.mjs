// End to end against the demo container:
//   docker run -d -p 18080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
//   JUMPSEAT_URL=http://localhost:18080 JUMPSEAT_TOKEN=secret npm test
// A real browser opens the demo viewer and checks the stream, the agent API
// and taking over. Exits 1 on any failure.
import { execFileSync } from "node:child_process";

import { chromium } from "playwright";

const base = process.env.JUMPSEAT_URL ?? "http://localhost:18080";
const token = process.env.JUMPSEAT_TOKEN ?? "secret";
const container = process.env.JUMPSEAT_CONTAINER ?? "jumpseat-test";
let failures = 0;
const check = (ok, what) => {
  failures += ok ? 0 : 1;
  console.log(`${ok ? "OK " : "BAD"} ${what}`);
};
const api = (method, path, body) =>
  fetch(`${base}${path}`, {
    method,
    headers: { authorization: `Bearer ${token}`, "content-type": "application/json" },
    body: body ? JSON.stringify(body) : undefined,
  });
const windowTitle = () =>
  execFileSync("docker", ["exec", "-e", "DISPLAY=:0", container, "xdotool", "getactivewindow", "getwindowname"], { encoding: "utf8" }).trim();

check((await fetch(`${base}/agent/lease`)).status === 401, "the agent API needs the token");

// Real Chrome: Playwright's own Chromium build has no H.264 decoder.
const browser = await chromium.launch({ channel: process.env.JUMPSEAT_BROWSER ?? "chrome" });
const page = await browser.newPage({ viewport: { width: 1280, height: 852 } });
await page.goto(`${base}/?token=${token}`);
await page.getByText("Agent in control").waitFor({ timeout: 30000 });
await page.waitForTimeout(1500);
const painted = await page.evaluate(() => {
  const canvas = document.querySelector("canvas");
  const probe = document.createElement("canvas");
  probe.width = 64;
  probe.height = 40;
  const ctx = probe.getContext("2d");
  ctx.drawImage(canvas, 0, 0, 64, 40);
  const data = ctx.getImageData(0, 0, 64, 40).data;
  let lit = 0;
  for (let i = 0; i < data.length; i += 4) lit += data[i] + data[i + 1] + data[i + 2] > 60 ? 1 : 0;
  return lit / (data.length / 4);
});
check(painted > 0.3, `the desktop streams (${Math.round(painted * 100)}% of the picture lit)`);

await api("POST", "/agent/status", { kind: "acting", text: "Opening example.com" });
check(await page.getByText("Opening example.com").waitFor({ timeout: 3000 }).then(() => true, () => false), "the agent's caption shows");
await api("POST", "/agent/status", { kind: "handoff", text: "Sign in to the demo" });
check(await page.getByText("Your turn: Sign in to the demo").waitFor({ timeout: 3000 }).then(() => true, () => false), "a handoff shows the user's turn");

// Take over and type into Chromium's address bar: the VM's window title follows.
await page.getByRole("button", { name: "Take over" }).click();
await page.mouse.click(640, 500);
await page.keyboard.press("Control+l");
// A person's pause: Chromium focuses its address bar a moment after Ctrl+L.
await page.waitForTimeout(300);
await page.keyboard.type("about:blank");
await page.keyboard.press("Enter");
await page.waitForTimeout(1500);
const title = windowTitle();
check(/about:blank/i.test(title), `taking over types into the VM (window: ${title})`);
const lease = await (await api("GET", "/agent/lease")).json();
check(lease.driving === true, `the agent sees the user driving: ${JSON.stringify(lease)}`);

await page.getByRole("button", { name: "Done, continue" }).click();
await page.waitForTimeout(500);
const after = await (await api("GET", "/agent/lease")).json();
check(after.answer === "done", `Done reaches the agent: ${JSON.stringify(after)}`);

// /agent/wait: XDamage tells the agent when the screen settles or changes.
const wait = async (query) => (await api("GET", `/agent/wait?${query}`)).json();
const still = await wait("idle=300&timeout=5000");
check(!still.timedOut && still.waitedMs < 5000, `a still screen settles: ${JSON.stringify(still)}`);
const nothing = await wait("change&timeout=400");
check(nothing.timedOut && !nothing.changed, `no change, no answer before the timeout: ${JSON.stringify(nothing)}`);
const pending = wait("change&timeout=5000");
await new Promise((done) => setTimeout(done, 300));
execFileSync("docker", ["exec", "-e", "DISPLAY=:0", container, "xdotool", "key", "ctrl+l", "type", "jumpseat wait"]);
const typed = await pending;
check(typed.changed && !typed.timedOut, `typing on the desktop ends a change wait: ${JSON.stringify(typed)}`);

await browser.close();
process.exit(failures ? 1 : 0);
