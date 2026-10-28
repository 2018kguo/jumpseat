// agentview.ts
var DESKTOP_WIDTH = 1280;
var DESKTOP_HEIGHT = 800;
function desktopPoint(rect, clientX, clientY) {
  const scale = Math.min(rect.width / DESKTOP_WIDTH, rect.height / DESKTOP_HEIGHT);
  if (scale <= 0) {
    return null;
  }
  const left = rect.left + (rect.width - DESKTOP_WIDTH * scale) / 2;
  const top = rect.top + (rect.height - DESKTOP_HEIGHT * scale) / 2;
  const x = Math.floor((clientX - left) / scale);
  const y = Math.floor((clientY - top) / scale);
  return x >= 0 && y >= 0 && x < DESKTOP_WIDTH && y < DESKTOP_HEIGHT ? { x, y } : null;
}
function desktopKey(event) {
  if (event.key === "Meta") {
    return "Control";
  }
  if ((event.ctrlKey || event.metaKey) && !event.altKey && event.key.length === 1 && event.key.charCodeAt(0) > 127) {
    const latin = /^(?:Key([A-Z])|Digit([0-9]))$/.exec(event.code);
    if (latin) {
      return (latin[1] ?? latin[2] ?? "").toLowerCase();
    }
  }
  return event.key;
}
var STATUS_KINDS = new Set(["idle", "acting", "waiting", "handoff"]);
function parseStreamText(raw) {
  let value;
  try {
    value = JSON.parse(raw);
  } catch {
    return null;
  }
  if (typeof value !== "object" || value === null) {
    return null;
  }
  const m = value;
  const num = (v) => typeof v === "number" && Number.isFinite(v);
  const str = (v) => typeof v === "string";
  if (m.t === "p" && num(m.x) && num(m.y)) {
    return { t: "p", x: m.x, y: m.y };
  }
  if (m.t === "c" && str(m.text)) {
    return { t: "c", text: m.text };
  }
  if (m.t === "status" && STATUS_KINDS.has(m.kind) && str(m.text)) {
    return { t: "status", kind: m.kind, text: m.text };
  }
  if (m.t === "cursor" && str(m.png) && num(m.x) && num(m.y)) {
    return { t: "cursor", png: m.png, x: m.x, y: m.y };
  }
  return null;
}
var HEARTBEAT_MS = 5000;
var FIRST_FRAME_MS = 20000;
var X_BUTTONS = { 0: 1, 1: 2, 2: 3 };
var POINTER_ARROW = `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='20'%3E%3Cpath d='M1 1v15l4-4 3 7 3-1-3-7h6z' fill='white' stroke='black'/%3E%3C/svg%3E")`;
function canDecodeDesktop() {
  return typeof window !== "undefined" && "VideoDecoder" in window;
}
var STREAM_CODEC = "avc1.42E028";
async function supportsStream() {
  if (!canDecodeDesktop()) {
    return false;
  }
  try {
    return (await VideoDecoder.isConfigSupported({ codec: STREAM_CODEC })).supported === true;
  } catch {
    return false;
  }
}
function connectDesktopStream(container, socketUrl, on) {
  const canvas = document.createElement("canvas");
  canvas.width = DESKTOP_WIDTH;
  canvas.height = DESKTOP_HEIGHT;
  canvas.tabIndex = 0;
  canvas.style.cssText = "width:100%;height:100%;object-fit:contain;outline:none;display:block";
  const pointer = document.createElement("div");
  pointer.style.cssText = `position:absolute;width:16px;height:20px;pointer-events:none;display:none;background:${POINTER_ARROW} no-repeat;background-size:contain`;
  let cursor = { css: "default", height: 20, width: 16, x: 0, y: 0 };
  const label = document.createElement("span");
  label.textContent = "Agent";
  label.style.cssText = "position:absolute;left:100%;top:100%;padding:1px 6px;border-radius:9999px;background:#4f46e5;color:#fff;font:600 11px/16px system-ui,sans-serif;white-space:nowrap";
  pointer.append(label);
  container.style.position = "relative";
  container.append(canvas, pointer);
  const context = canvas.getContext("2d");
  let closed = false;
  let controlling = false;
  let configured = false;
  let connected = false;
  let copiedAt = 0;
  const decoder = new VideoDecoder({
    output: (frame) => {
      context?.drawImage(frame, 0, 0);
      sendAlways({ t: "a", ts: Math.round(frame.timestamp / 1000) });
      frame.close();
    },
    error: () => drop()
  });
  const socket = new WebSocket(socketUrl);
  socket.binaryType = "arraybuffer";
  async function checkDecoderSupport() {
    if (!await supportsStream() && !closed) {
      closed = true;
      socket.close();
      (on.unsupported ?? on.dropped)();
    }
  }
  function drop() {
    if (!closed) {
      closed = true;
      clearInterval(heartbeat);
      clearTimeout(firstFrame);
      socket.close();
      on.dropped();
    }
  }
  const sendAlways = (message) => {
    if (socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(message));
    }
  };
  const send = (message) => {
    if (controlling) {
      sendAlways(message);
    }
  };
  const heartbeat = setInterval(() => sendAlways({ t: "h" }), HEARTBEAT_MS);
  const firstFrame = setTimeout(drop, FIRST_FRAME_MS);
  socket.addEventListener("message", (event) => {
    if (typeof event.data === "string") {
      const message = parseStreamText(event.data);
      if (!message) {
        return;
      }
      if (message.t === "cursor") {
        const url = `url(data:image/png;base64,${message.png})`;
        cursor = {
          css: `${url} ${message.x} ${message.y}, default`,
          height: cursor.height,
          width: cursor.width,
          x: message.x,
          y: message.y
        };
        pointer.style.background = `${url} no-repeat`;
        pointer.style.backgroundSize = "contain";
        const image = new Image;
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
        const scale = Math.min(rect.width / DESKTOP_WIDTH, rect.height / DESKTOP_HEIGHT);
        pointer.style.left = `${(rect.width - DESKTOP_WIDTH * scale) / 2 + (message.x - cursor.x) * scale}px`;
        pointer.style.top = `${(rect.height - DESKTOP_HEIGHT * scale) / 2 + (message.y - cursor.y) * scale}px`;
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
      checkDecoderSupport();
      decoder.configure({
        codec: STREAM_CODEC,
        optimizeForLatency: true
      });
      configured = true;
    }
    decoder.decode(new EncodedVideoChunk({
      data: data.subarray(9),
      timestamp: stamp * 1000,
      type: key ? "key" : "delta"
    }));
    if (!connected) {
      connected = true;
      clearTimeout(firstFrame);
      on.connected();
    }
  });
  socket.addEventListener("close", drop);
  socket.addEventListener("error", drop);
  const at = (event) => desktopPoint(canvas.getBoundingClientRect(), event.clientX, event.clientY);
  canvas.addEventListener("pointermove", (event) => {
    if (event.pointerType === "touch") {
      return;
    }
    const point = at(event);
    if (point) {
      send({ t: "m", ...point });
    }
  });
  const onButton = (down) => (event) => {
    const button = X_BUTTONS[event.button];
    if (!controlling || button === undefined || event.pointerType === "touch") {
      return;
    }
    const rect = canvas.getBoundingClientRect();
    const point = at(event) ?? (down ? null : desktopPoint(rect, Math.min(Math.max(event.clientX, rect.left), rect.right - 1), Math.min(Math.max(event.clientY, rect.top), rect.bottom - 1)));
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
    send
  });
  canvas.addEventListener("wheel", (event) => {
    if (!controlling) {
      return;
    }
    send({
      t: "w",
      dx: Math.sign(event.deltaX),
      dy: Math.sign(event.deltaY)
    });
    event.preventDefault();
  }, { passive: false });
  canvas.addEventListener("contextmenu", (event) => {
    if (controlling) {
      event.preventDefault();
    }
  });
  const onKey = (down) => (event) => {
    if (!controlling) {
      return;
    }
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "v") {
      return;
    }
    if (event.repeat) {
      event.preventDefault();
      return;
    }
    if (down && event.key === "AltGraph") {
      send({ t: "k", key: "Control", down: false });
    }
    if (down && (event.metaKey || event.ctrlKey) && /^[cx]$/i.test(event.key)) {
      copiedAt = performance.now();
    }
    send({ t: "k", key: desktopKey(event), down });
    if (!down && event.key === "Meta") {
      send({ t: "r" });
    }
    event.preventDefault();
  };
  canvas.addEventListener("keydown", onKey(true));
  canvas.addEventListener("keyup", onKey(false));
  canvas.addEventListener("blur", () => send({ t: "r" }));
  const onPaste = (event) => {
    const text = event.clipboardData?.getData("text/plain");
    if (!controlling || document.activeElement !== canvas || !text || text.length > 64 * 1024) {
      return;
    }
    send({ t: "c", text });
    for (const [key, down] of [
      ["Control", true],
      ["v", true],
      ["v", false],
      ["Control", false]
    ]) {
      send({ t: "k", key, down });
    }
    event.preventDefault();
  };
  document.addEventListener("paste", onPaste);
  return {
    close() {
      closed = true;
      clearInterval(heartbeat);
      clearTimeout(firstFrame);
      socket.close();
      if (decoder.state !== "closed") {
        decoder.close();
      }
      document.removeEventListener("paste", onPaste);
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
    }
  };
}
var LONG_PRESS_MS = 600;
var TAP_SLOP_PX = 10;
var SCROLL_STEP_PX = 40;
function attachTouch(canvas, view) {
  canvas.style.touchAction = "none";
  const fingers = new Map;
  let start = null;
  let pressed = false;
  let longPress;
  let scrolled = 0;
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
      if (pressed && start) {
        view.send({ t: "b", b: 1, down: false, x: start.x, y: start.y });
        pressed = false;
      }
      reset();
      scrolled = 0;
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
      scrolled += last.y - event.clientY;
      while (Math.abs(scrolled) >= SCROLL_STEP_PX) {
        view.send({ t: "w", dx: 0, dy: Math.sign(scrolled) });
        scrolled -= Math.sign(scrolled) * SCROLL_STEP_PX;
      }
      return;
    }
    if (!start) {
      return;
    }
    const point = view.at(event);
    if (!pressed && Math.hypot(event.clientX - start.clientX, event.clientY - start.clientY) > TAP_SLOP_PX) {
      clearTimeout(longPress);
      view.send({ t: "b", b: 1, down: true, x: start.x, y: start.y });
      pressed = true;
    }
    if (pressed && point) {
      view.send({ t: "m", ...point });
    }
  });
  const lift = (event) => {
    if (event.pointerType !== "touch" || !fingers.delete(event.pointerId)) {
      return;
    }
    if (start && fingers.size === 0) {
      const point = view.at(event) ?? start;
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
var connect = connectDesktopStream;
export {
  DESKTOP_HEIGHT,
  DESKTOP_WIDTH,
  STREAM_CODEC,
  canDecodeDesktop,
  connect,
  connectDesktopStream,
  desktopKey,
  desktopPoint,
  supportsStream
};
