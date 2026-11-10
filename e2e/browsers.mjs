// Decoding in every engine: the replay demo (docs/demo, a recorded stream fed
// to the bundled client) played in WebKit, Firefox and Chrome, each of which
// must decode every frame without a decoder error.
//   npx playwright install webkit firefox
//   node browsers.mjs [demo URL, default https://kguo.dev/jumpseat/]
// Exits 1 on any failure.
import { chromium, firefox, webkit } from "playwright";

const url = process.argv[2] ?? "https://kguo.dev/jumpseat/";
let failures = 0;
for (const [name, type, options] of [
  ["webkit", webkit, {}],
  ["firefox", firefox, {}],
  // Playwright's own Chromium has no H.264 decoder.
  ["chrome", chromium, { channel: "chrome" }],
]) {
  const browser = await type.launch(options);
  const page = await browser.newPage();
  // Frames per decoder: the replay makes a new one each time it starts over.
  await page.addInitScript(() => {
    window.decoded = { frames: [], errors: [] };
    const Decoder = window.VideoDecoder;
    if (!Decoder) return;
    window.VideoDecoder = class extends Decoder {
      constructor(init) {
        const n = window.decoded.frames.push(0) - 1;
        super({
          output: (frame) => (window.decoded.frames[n]++, init.output(frame)),
          error: (error) => (window.decoded.errors.push(String(error)), init.error(error)),
        });
      }
    };
  });
  await page.goto(url);
  // Done when the replay starts over: the first decoder saw the whole stream.
  await page.waitForFunction(() => window.decoded.frames.length > 1, null, { timeout: 120_000 }).catch(() => {});
  const { frames: [frames = 0], errors } = await page.evaluate(() => window.decoded);
  const ok = frames > 100 && errors.length === 0;
  failures += ok ? 0 : 1;
  console.log(`${ok ? "OK " : "BAD"} ${name}: ${frames} frames decoded${errors.length ? `, errors: ${errors.join("; ")}` : ""}`);
  await browser.close();
}
process.exit(failures ? 1 : 0);
