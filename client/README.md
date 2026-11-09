# @2018kguo/jumpseat

The browser client for [jumpseat](https://github.com/2018kguo/jumpseat): watch a
computer-use agent's Linux desktop live (H.264 over one websocket, decoded with
WebCodecs) and take over when it needs you. No dependencies.

```ts
import { connect, supportsStream } from "@2018kguo/jumpseat";

if (!(await supportsStream())) showFallback(); // no WebCodecs H.264 decoder
const view = connect(element, `wss://host/stream?token=${token}`, {
  connected: () => {},
  dropped: () => {},
  status: ({ kind, text }) => {}, // "acting" | "waiting" | "handoff" | "idle"
});
view.setControlling(true); // the person's keys, pointer and scrolls reach the desktop
```

The server, the agent API and every option are in
[the repository](https://github.com/2018kguo/jumpseat#readme) and its
[reference](https://github.com/2018kguo/jumpseat/blob/main/docs/reference.md).
