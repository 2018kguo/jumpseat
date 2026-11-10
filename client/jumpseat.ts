/**
 * The browser end of jumpseat: H.264 frames decoded with
 * WebCodecs onto a canvas, and the user's input sent back while they have
 * taken over. The wire format is in docs/reference.md.
 */

/** The desktop's size until the server says otherwise. */
const DESKTOP_WIDTH = 1280;
const DESKTOP_HEIGHT = 800;

type Size = { height: number; width: number };

/** What the agent is doing on the desktop, as the VM reports it. */
export type AgentStatus = {
  kind: "idle" | "acting" | "waiting" | "handoff";
  text: string;
};

export type DesktopStream = {
  close(): void;
  setControlling(controlling: boolean): void;
};

type Rect = { height: number; left: number; top: number; width: number };

/**
 * The desktop pixel under a client point, or null outside the picture. The
 * canvas is letterboxed (object-fit: contain), so the picture is the largest
 * box of the desktop's size centered in the element.
 */
function desktopPoint(
  rect: Rect,
  clientX: number,
  clientY: number,
  size: Size = { height: DESKTOP_HEIGHT, width: DESKTOP_WIDTH }
): { x: number; y: number } | null {
  const scale = Math.min(rect.width / size.width, rect.height / size.height);
  if (scale <= 0) {
    return null;
  }
  const left = rect.left + (rect.width - size.width * scale) / 2;
  const top = rect.top + (rect.height - size.height * scale) / 2;
  const x = Math.floor((clientX - left) / scale);
  const y = Math.floor((clientY - top) / scale);
  return x >= 0 && y >= 0 && x < size.width && y < size.height
    ? { x, y }
    : null;
}

/**
 * The key the VM gets for a KeyboardEvent.key. Meta is Control there, so
 * Cmd+A, Cmd+L and Cmd+C on a Mac do what they do at home.
 */
function desktopKey(
  event: Pick<KeyboardEvent, "altKey" | "code" | "ctrlKey" | "key" | "metaKey">
): string {
  if (event.key === "Meta") {
    return "Control";
  }
  // A shortcut on a non-Latin layout (Ctrl+C on Russian gives "с") goes by
  // the physical key, as the VM's US keymap needs. Not with Alt: Windows
  // reports AltGr as Ctrl+Alt, and AltGr+A typing "ą" is no shortcut.
  if (
    (event.ctrlKey || event.metaKey) &&
    !event.altKey &&
    event.key.length === 1 &&
    event.key.charCodeAt(0) > 0x7f
  ) {
    const latin = /^(?:Key([A-Z])|Digit([0-9]))$/.exec(event.code);
    if (latin) {
      return (latin[1] ?? latin[2] ?? "").toLowerCase();
    }
  }
  return event.key;
}

// The streamer's text messages: the agent's pointer, and the VM's clipboard.
type StreamText =
  | { t: "p"; x: number; y: number }
  | { t: "c"; text: string }
  | { t: "status"; kind: AgentStatus["kind"]; text: string }
  | { t: "cursor"; png: string; x: number; y: number }
  | { t: "size"; w: number; h: number };

const STATUS_KINDS = new Set(["idle", "acting", "waiting", "handoff"]);

/**
 * The server's text messages (see docs/reference.md): the agent's pointer, the
 * VM's clipboard, the agent's status, the cursor image. Anything else is
 * ignored, so a newer server never breaks an older client.
 */
function parseStreamText(raw: string): StreamText | null {
  let value: unknown;
  try {
    value = JSON.parse(raw);
  } catch {
    return null;
  }
  if (typeof value !== "object" || value === null) {
    return null;
  }
  const m = value as Record<string, unknown>;
  const num = (v: unknown) => typeof v === "number" && Number.isFinite(v);
  const str = (v: unknown) => typeof v === "string";
  if (m.t === "p" && num(m.x) && num(m.y)) {
    return { t: "p", x: m.x as number, y: m.y as number };
  }
  if (m.t === "c" && str(m.text)) {
    return { t: "c", text: m.text as string };
  }
  if (m.t === "status" && STATUS_KINDS.has(m.kind as string) && str(m.text)) {
    return { t: "status", kind: m.kind as AgentStatus["kind"], text: m.text as string };
  }
  if (m.t === "size" && num(m.w) && num(m.h) && (m.w as number) > 0 && (m.h as number) > 0) {
    return { t: "size", w: m.w as number, h: m.h as number };
  }
  if (m.t === "cursor" && str(m.png) && num(m.x) && num(m.y)) {
    return { t: "cursor", png: m.png as string, x: m.x as number, y: m.y as number };
  }
  return null;
}

// The streamer drops a viewer silent for 20 s, so it heartbeats well inside
// that; a socket with no frame by FIRST_FRAME_MS is treated as dropped.
const HEARTBEAT_MS = 5000;
const FIRST_FRAME_MS = 20_000;

// Mouse buttons as X numbers them: 1 left, 2 middle, 3 right.
const X_BUTTONS: Record<number, number> = { 0: 1, 1: 2, 2: 3 };

const POINTER_ARROW =
  "url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='20'%3E%3Cpath d='M1 1v15l4-4 3 7 3-1-3-7h6z' fill='white' stroke='black'/%3E%3C/svg%3E\")";

/** The codec the server sends: H.264 High profile (CABAC, no B-frames). */
const STREAM_CODEC = "avc1.640029";

/**
 * Whether this browser decodes the stream: WebCodecs exists and has an H.264
 * decoder (some Linux Chromium builds ship without one).
 */
export async function supportsStream(): Promise<boolean> {
  if (typeof VideoDecoder === "undefined") {
    return false;
  }
  try {
    return (await VideoDecoder.isConfigSupported({ codec: STREAM_CODEC })).supported === true;
  } catch {
    return false;
  }
}

/**
 * Streams the desktop at socketUrl into container, which it fills. A browser
 * that cannot decode the stream gets on.unsupported (or on.dropped) instead.
 */
export function connect(
  container: HTMLElement,
  socketUrl: string,
  on: {
    connected: () => void;
    dropped: () => void;
    status?: (status: AgentStatus) => void;
    /** The browser cannot decode H.264: the stream stops instead of dropping. */
    unsupported?: () => void;
  },
  /**
   * resize: ask the desktop to take the element's size, in CSS pixels, as it
   * changes. The server honors it only with --resize.
   */
  options: { resize?: boolean } = {}
): DesktopStream {
  if (typeof VideoDecoder === "undefined") {
    setTimeout(on.unsupported ?? on.dropped);
    return { close() {}, setControlling() {} };
  }
  const canvas = document.createElement("canvas");
  // The desktop's size: the server sends it before each new stream's keyframe.
  const size: Size = { height: DESKTOP_HEIGHT, width: DESKTOP_WIDTH };
  canvas.width = size.width;
  canvas.height = size.height;
  canvas.tabIndex = 0;
  canvas.style.cssText =
    "width:100%;height:100%;object-fit:contain;outline:none;display:block";
  // The agent's pointer: the stream carries its position, not its pixels.
  const pointer = document.createElement("div");
  pointer.style.cssText = `position:absolute;width:16px;height:20px;pointer-events:none;display:none;background:${POINTER_ARROW} no-repeat;background-size:contain`;
  // The VM's cursor, as a CSS cursor while in control and as the agent's
  // pointer while watching.
  let cursor = { css: "default", height: 20, width: 16, x: 0, y: 0 };
  // Whose pointer it is: the agent's, while the user watches.
  const label = document.createElement("span");
  label.textContent = "Agent";
  label.style.cssText =
    "position:absolute;left:100%;top:100%;padding:1px 6px;border-radius:9999px;background:#4f46e5;color:#fff;font:600 11px/16px system-ui,sans-serif;white-space:nowrap";
  pointer.append(label);
  container.style.position = "relative";
  container.append(canvas, pointer);
  // Not desynchronized: same-VM A/B showed no latency gain (README).
  const context = canvas.getContext("2d");

  let closed = false;
  let controlling = false;
  let configured = false;
  let connected = false;
  // When the user last pressed copy or cut in the viewer: only then may the
  // VM's clipboard reach theirs. A page in the VM copying on its own must
  // not plant text in the user's clipboard.
  let copiedAt = 0;
  const decoder = new VideoDecoder({
    output: (frame) => {
      if (canvas.width !== frame.displayWidth || canvas.height !== frame.displayHeight) {
        canvas.width = frame.displayWidth;
        canvas.height = frame.displayHeight;
      }
      context?.drawImage(frame, 0, 0);
      // The frame's id (the streamer's ms stamp) back as shown: the streamer
      // holds capture while too much is unshown, so a slow link costs
      // frames, not seconds of input lag (server/flow.go).
      sendAlways({ t: "a", ts: Math.round(frame.timestamp / 1000) });
      frame.close();
    },
    error: () => drop(),
  });
  const socket = new WebSocket(socketUrl);
  socket.binaryType = "arraybuffer";

  function stop(notify: () => void) {
    if (!closed) {
      closed = true;
      clearInterval(heartbeat);
      clearTimeout(firstFrame);
      resizer?.disconnect();
      socket.close();
      notify();
    }
  }
  const drop = () => stop(on.dropped);
  // A browser without an H.264 decoder says so instead of looking like a
  // dropped connection.
  void supportsStream().then((ok) => ok || stop(on.unsupported ?? on.dropped));
  const sendAlways = (message: object) => {
    if (socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(message));
    }
  };
  const send = (message: object) => {
    if (controlling) {
      sendAlways(message);
    }
  };
  const heartbeat = setInterval(() => sendAlways({ t: "h" }), HEARTBEAT_MS);
  let firstFrame = setTimeout(drop, FIRST_FRAME_MS);

  socket.addEventListener("message", (event) => {
    if (typeof event.data === "string") {
      const message = parseStreamText(event.data);
      if (!message) {
        return;
      }
      if (message.t === "size") {
        // The keyframe that follows starts a stream of the new size. The
        // canvas changes size when that frame is drawn: resizing it now
        // would blank the picture until then.
        size.width = message.w;
        size.height = message.h;
        configured = false;
      } else if (message.t === "cursor") {
        const url = `url(data:image/png;base64,${message.png})`;
        cursor = {
          css: `${url} ${message.x} ${message.y}, default`,
          height: cursor.height,
          width: cursor.width,
          x: message.x,
          y: message.y,
        };
        pointer.style.background = `${url} no-repeat`;
        pointer.style.backgroundSize = "contain";
        const image = new Image();
        image.addEventListener("load", () => {
          cursor.width = image.width;
          cursor.height = image.height;
        });
        image.src = `data:image/png;base64,${message.png}`;
        if (controlling) {
          canvas.style.cursor = cursor.css;
        }
      } else if (message.t === "status") {
        on.status?.({ kind: message.kind, text: message.text });
      } else if (message.t === "p") {
        const rect = canvas.getBoundingClientRect();
        const scale = Math.min(
          rect.width / size.width,
          rect.height / size.height
        );
        pointer.style.left = `${(rect.width - size.width * scale) / 2 + (message.x - cursor.x) * scale}px`;
        pointer.style.top = `${(rect.height - size.height * scale) / 2 + (message.y - cursor.y) * scale}px`;
        // Drawn at the picture's scale, hotspot and all.
        pointer.style.width = `${cursor.width * scale}px`;
        pointer.style.height = `${cursor.height * scale}px`;
        pointer.style.display = controlling ? "none" : "block";
      } else if (controlling && performance.now() - copiedAt < 2000) {
        navigator.clipboard?.writeText(message.text).catch(() => {});
      }
      return;
    }
    if (!(event.data instanceof ArrayBuffer)) {
      return;
    }
    const data = new Uint8Array(event.data);
    const key = data[0] === 1;
    const stamp = Number(new DataView(event.data).getBigUint64(1));
    if (!configured) {
      if (!key) {
        return;
      }
      decoder.configure({
        codec: STREAM_CODEC,
        optimizeForLatency: true,
      });
      configured = true;
    }
    decoder.decode(
      new EncodedVideoChunk({
        data: data.subarray(9),
        timestamp: stamp * 1000,
        type: key ? "key" : "delta",
      })
    );
    if (!connected) {
      connected = true;
      clearTimeout(firstFrame);
      on.connected();
    }
  });
  socket.addEventListener("close", drop);
  // The desktop follows the element's size, once it has held still a moment.
  let resizeTimer: ReturnType<typeof setTimeout> | undefined;
  const askSize = () => {
    const { width, height } = container.getBoundingClientRect();
    sendAlways({ t: "z", w: Math.round(width), h: Math.round(height) });
  };
  const resizer = options.resize
    ? new ResizeObserver(() => {
        clearTimeout(resizeTimer);
        resizeTimer = setTimeout(askSize, 200);
      })
    : null;
  resizer?.observe(container);
  if (resizer) {
    socket.addEventListener("open", askSize);
  }
  socket.addEventListener("error", drop);

  const at = (event: { clientX: number; clientY: number }) =>
    desktopPoint(canvas.getBoundingClientRect(), event.clientX, event.clientY, size);
  canvas.addEventListener("pointermove", (event) => {
    if (event.pointerType === "touch") {
      return;
    }
    const point = at(event);
    if (point) {
      send({ t: "m", ...point });
    }
  });
  const onButton = (down: boolean) => (event: PointerEvent) => {
    const button = X_BUTTONS[event.button];
    if (!controlling || button === undefined || event.pointerType === "touch") {
      return;
    }
    // A release outside the picture still releases, at the nearest edge, or
    // the VM keeps the button down and the agent's next click drags.
    const rect = canvas.getBoundingClientRect();
    const point =
      at(event) ??
      (down
        ? null
        : desktopPoint(
            rect,
            Math.min(Math.max(event.clientX, rect.left), rect.right - 1),
            Math.min(Math.max(event.clientY, rect.top), rect.bottom - 1),
            size
          ));
    if (!point) {
      return;
    }
    if (down) {
      canvas.focus();
      canvas.setPointerCapture(event.pointerId);
    }
    send({ t: "b", b: button, down, ...point });
    event.preventDefault();
  };
  canvas.addEventListener("pointerdown", onButton(true));
  canvas.addEventListener("pointerup", onButton(false));
  attachTouch(canvas, {
    at,
    controlling: () => controlling,
    send,
  });
  canvas.addEventListener(
    "wheel",
    (event) => {
      if (!controlling) {
        return;
      }
      // Desktop pixels, so a trackpad scrolls the desktop pixel by pixel:
      // lines and pages as Chromium counts them, then the canvas's scale.
      const unit =
        (event.deltaMode === 1 ? 40 : event.deltaMode === 2 ? 800 : 1) *
        desktopPerCssPixel(canvas);
      send({ t: "w", sx: event.deltaX * unit, sy: event.deltaY * unit });
      event.preventDefault();
    },
    { passive: false }
  );
  canvas.addEventListener("contextmenu", (event) => {
    if (controlling) {
      event.preventDefault();
    }
  });
  const onKey = (down: boolean) => (event: KeyboardEvent) => {
    if (!controlling) {
      return;
    }
    // Cmd/Ctrl+V pastes this machine's clipboard: the paste event below
    // sends the text, then presses Ctrl+V in the VM. Any case: with Shift or
    // CapsLock the key is "V", and forwarding it pasted the VM's own, stale
    // clipboard instead.
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "v") {
      return;
    }
    // X repeats a held key itself; the browser's repeats would double it.
    if (event.repeat) {
      event.preventDefault();
      return;
    }
    // Windows sends AltGr as Control then AltGraph: let go of that Control,
    // or AltGr+Q's "@" reaches the VM as Ctrl+@.
    if (down && event.key === "AltGraph") {
      send({ t: "k", key: "Control", down: false });
    }
    if (down && (event.metaKey || event.ctrlKey) && /^[cx]$/i.test(event.key)) {
      copiedAt = performance.now();
    }
    send({ t: "k", key: desktopKey(event), down });
    // A Mac sends no keyup for a key held with Cmd: let go of everything
    // when Cmd comes up, so Cmd+A does not leave "a" held in the VM.
    if (!down && event.key === "Meta") {
      send({ t: "r" });
    }
    event.preventDefault();
  };
  canvas.addEventListener("keydown", onKey(true));
  canvas.addEventListener("keyup", onKey(false));
  // Focus leaving mid-press (Cmd+Tab) never delivers the keyup.
  canvas.addEventListener("blur", () => send({ t: "r" }));
  // On the document, not the canvas: a paste fires at the document when the
  // focused element (this canvas) is not editable, so a listener on the
  // canvas never heard one and the VM pasted its own, stale clipboard.
  const onPaste = (event: ClipboardEvent) => {
    const text = event.clipboardData?.getData("text/plain");
    // The streamer refuses more than 64 KiB; pressing Ctrl+V anyway would
    // paste whatever the VM's clipboard held before.
    if (
      !controlling ||
      document.activeElement !== canvas ||
      !text ||
      text.length > 64 * 1024
    ) {
      return;
    }
    send({ t: "c", text });
    for (const [key, down] of [
      ["Control", true],
      ["v", true],
      ["v", false],
      ["Control", false],
    ] as const) {
      send({ t: "k", key, down });
    }
    event.preventDefault();
  };
  document.addEventListener("paste", onPaste);
  // A background tab gets no frames: the server stops encoding for it and
  // sends a keyframe when it is shown again.
  const onVisibility = () => {
    sendAlways({ t: "v", hidden: document.hidden });
    // No frames come while hidden: the first-frame watchdog waits until shown.
    clearTimeout(firstFrame);
    if (!document.hidden && !connected && !closed) {
      firstFrame = setTimeout(drop, FIRST_FRAME_MS);
    }
  };
  document.addEventListener("visibilitychange", onVisibility);
  // A page opened in a background tab says so as soon as it can.
  socket.addEventListener("open", () => {
    if (document.hidden) {
      onVisibility();
    }
  });

  return {
    close() {
      closed = true;
      clearInterval(heartbeat);
      clearTimeout(firstFrame);
      resizer?.disconnect();
      socket.close();
      if (decoder.state !== "closed") {
        decoder.close();
      }
      document.removeEventListener("paste", onPaste);
      document.removeEventListener("visibilitychange", onVisibility);
      canvas.remove();
      pointer.remove();
    },
    setControlling(next) {
      if (!next) {
        send({ t: "r" });
      }
      controlling = next;
      pointer.style.display = "none";
      canvas.style.cursor = next ? cursor.css : "default";
      if (next) {
        canvas.focus();
      }
    },
  };
}

// How long a still finger must stay down to right-click, how far it may
// drift and still be a tap.
const LONG_PRESS_MS = 600;
const TAP_SLOP_PX = 10;

/**
 * Touch, so a phone opening the live-view link can act too (selkies'
 * mapping): a tap clicks, a long press right-clicks, one finger moving
 * drags, two fingers moving scroll. The press is held back until it is clear
 * which, so a long press never clicks a link first.
 */
function attachTouch(
  canvas: HTMLCanvasElement,
  view: {
    at: (event: {
      clientX: number;
      clientY: number;
    }) => { x: number; y: number } | null;
    controlling: () => boolean;
    send: (message: object) => void;
  }
) {
  canvas.style.touchAction = "none";
  const fingers = new Map<number, { x: number; y: number }>();
  let start: { clientX: number; clientY: number; x: number; y: number } | null =
    null;
  let pressed = false;
  let longPress: ReturnType<typeof setTimeout> | undefined;
  const reset = () => {
    clearTimeout(longPress);
    start = null;
  };
  canvas.addEventListener("pointerdown", (event) => {
    if (event.pointerType !== "touch" || !view.controlling()) {
      return;
    }
    event.preventDefault();
    fingers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (fingers.size > 1) {
      // A second finger: scrolling, not a press.
      if (pressed && start) {
        view.send({ t: "b", b: 1, down: false, x: start.x, y: start.y });
        pressed = false;
      }
      reset();
      return;
    }
    const point = view.at(event);
    if (!point) {
      return;
    }
    canvas.focus();
    start = { clientX: event.clientX, clientY: event.clientY, ...point };
    longPress = setTimeout(() => {
      if (start && !pressed) {
        view.send({ t: "b", b: 3, down: true, x: start.x, y: start.y });
        view.send({ t: "b", b: 3, down: false, x: start.x, y: start.y });
        start = null;
      }
    }, LONG_PRESS_MS);
  });
  canvas.addEventListener("pointermove", (event) => {
    const last = fingers.get(event.pointerId);
    if (event.pointerType !== "touch" || !last || !view.controlling()) {
      return;
    }
    fingers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (fingers.size > 1) {
      // The content follows the fingers.
      const unit = desktopPerCssPixel(canvas);
      view.send({
        t: "w",
        sx: (last.x - event.clientX) * unit,
        sy: (last.y - event.clientY) * unit,
      });
      return;
    }
    if (!start) {
      return;
    }
    const point = view.at(event);
    if (
      !pressed &&
      Math.hypot(event.clientX - start.clientX, event.clientY - start.clientY) >
        TAP_SLOP_PX
    ) {
      clearTimeout(longPress);
      view.send({ t: "b", b: 1, down: true, x: start.x, y: start.y });
      pressed = true;
    }
    if (pressed && point) {
      view.send({ t: "m", ...point });
    }
  });
  const lift = (event: PointerEvent) => {
    if (event.pointerType !== "touch" || !fingers.delete(event.pointerId)) {
      return;
    }
    if (start && fingers.size === 0) {
      const point = view.at(event) ?? start;
      // A cancelled touch only lets go; it never taps.
      if (!pressed && event.type === "pointerup") {
        view.send({ t: "b", b: 1, down: true, x: start.x, y: start.y });
        view.send({ t: "b", b: 1, down: false, ...point });
      } else if (pressed) {
        view.send({ t: "b", b: 1, down: false, ...point });
      }
    }
    pressed = false;
    reset();
  };
  canvas.addEventListener("pointerup", lift);
  canvas.addEventListener("pointercancel", lift);
}

/** Desktop pixels per CSS pixel of the letterboxed canvas. */
function desktopPerCssPixel(canvas: HTMLCanvasElement) {
  const rect = canvas.getBoundingClientRect();
  return 1 / Math.min(rect.width / canvas.width, rect.height / canvas.height);
}

