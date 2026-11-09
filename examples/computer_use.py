"""Claude drives a jumpseat desktop while you watch, and hands you the steps
it can't do. Needs `pip install anthropic`, an API key, and the demo
running (see ../README.md#try-it):

    docker run -d --name jumpseat -p 8080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
    open "http://localhost:8080/?token=secret"
    JUMPSEAT_EXEC="docker exec jumpseat" python3 examples/computer_use.py "Find the weather in Paris"

Actions run as xdotool on the desktop's machine (JUMPSEAT_EXEC prefixes the
command; leave it empty on that machine). Screenshots, captions, waits and
the handoff go through jumpseat's agent API.
"""
import base64, json, os, shlex, subprocess, sys, time, urllib.request

import anthropic

URL = os.environ.get("JUMPSEAT_URL", "http://localhost:8080")
TOKEN = os.environ.get("JUMPSEAT_TOKEN", "secret")
EXEC = shlex.split(os.environ.get("JUMPSEAT_EXEC", ""))


def api(path, body=None):
    req = urllib.request.Request(URL + path, data=json.dumps(body).encode() if body else None,
                                 headers={"Authorization": f"Bearer {TOKEN}"})
    with urllib.request.urlopen(req) as r:
        return r.read()


def status(kind, text):
    api("/agent/status", {"kind": kind, "text": text[:200]})


def xdo(*args):
    subprocess.run([*EXEC, "env", "DISPLAY=:0", "xdotool", *map(str, args)], check=True, capture_output=True)


def screenshot():
    api("/agent/wait")  # until the screen settles, not a fixed sleep
    data = base64.b64encode(api("/agent/screenshot")).decode()
    return [{"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": data}}]


def click(inp, button, repeat=1):
    if "coordinate" in inp:
        xdo("mousemove", *inp["coordinate"])
    mods = inp.get("text", "").split("+") if inp.get("text") else []
    for m in mods: xdo("keydown", m)
    xdo("click", "--repeat", repeat, button)
    for m in reversed(mods): xdo("keyup", m)


def act(name, inp):
    """One computer toolset action; returns the tool_result content."""
    # The person has the keyboard: don't fight them for it.
    while json.loads(api("/agent/lease"))["driving"]:
        time.sleep(1)
    match name:
        case "screenshot": return screenshot()
        case "left_click": click(inp, 1)
        case "middle_click": click(inp, 2)
        case "right_click": click(inp, 3)
        case "double_click": click(inp, 1, 2)
        case "triple_click": click(inp, 1, 3)
        case "mouse_move": xdo("mousemove", *inp["coordinate"])
        case "left_mouse_down": xdo("mousedown", 1)
        case "left_mouse_up": xdo("mouseup", 1)
        case "left_click_drag":
            xdo("mousemove", *inp["start_coordinate"]); xdo("mousedown", 1)
            xdo("mousemove", *inp["coordinate"]); xdo("mouseup", 1)
        case "scroll":
            if "coordinate" in inp: xdo("mousemove", *inp["coordinate"])
            button = {"up": 4, "down": 5, "left": 6, "right": 7}[inp["scroll_direction"]]
            xdo("click", "--repeat", inp["scroll_amount"], button)
        case "type": xdo("type", "--delay", 12, "--", inp["text"])
        case "key": xdo("key", "--repeat", inp.get("repeat", 1), inp["text"])
        case "hold_key":
            xdo("keydown", inp["text"]); time.sleep(inp["duration"]); xdo("keyup", inp["text"])
        case "wait": time.sleep(inp["duration"])
        case "cursor_position":
            out = subprocess.run([*EXEC, "env", "DISPLAY=:0", "xdotool", "getmouselocation", "--shell"],
                                 check=True, capture_output=True, text=True).stdout
            v = dict(l.split("=") for l in out.split())
            return f"[{v['X']}, {v['Y']}]"
        case _: raise ValueError(f"unsupported action {name}")
    return "OK"


def ask_person(text):
    """Hand a step to the person watching; returns when they answer."""
    status("handoff", text)
    while not (answer := json.loads(api("/agent/lease"))["answer"]):
        time.sleep(1)
    status("acting", "Taking back over")
    return f"The person answered: {answer}"


TOOLS = [
    {"type": "computer_toolset_20260801", "configs": {"zoom": {"enabled": False}}},
    {"name": "ask_person", "description": "Hand a step you can't or shouldn't do (a sign-in, a captcha, "
     "a payment, a judgment call) to the person watching the desktop. Returns once they click Done or Skip.",
     "input_schema": {"type": "object", "properties": {"text": {"type": "string", "description": "What they should do."}},
                      "required": ["text"], "additionalProperties": False}, "strict": True},
]


def main(task):
    client = anthropic.Anthropic()
    messages = [{"role": "user", "content": task}]
    while True:
        response = client.beta.messages.create(
            model="claude-opus-5-5", max_tokens=16000, tools=TOOLS, messages=messages,
            # Progress notes come back as short thinking summaries: they become captions.
            thinking={"type": "adaptive", "display": "updates"},
            betas=["thinking-display-updates-2026-08-18", "server-side-fallback-2026-07-01"],
            fallbacks="default",
        )
        messages.append({"role": "assistant", "content": response.content})
        results, failed = [], False
        for block in response.content:
            if block.type == "thinking" and block.thinking:
                status("acting", block.thinking)
            if block.type != "tool_use":
                continue
            result = {"type": "tool_result", "tool_use_id": block.id}
            if getattr(block, "toolset_name", None) == "computer":
                result["toolset_name"] = "computer"
                if failed:
                    result |= {"content": "Not executed: an earlier computer action in this turn failed.", "is_error": True}
                else:
                    try:
                        result["content"] = act(block.name, block.input)
                    except Exception as err:
                        result |= {"content": f"Error: {err}", "is_error": True}
                        failed = True
            else:
                result["content"] = ask_person(block.input["text"])
            results.append(result)
        if not results:
            text = "".join(b.text for b in response.content if b.type == "text")
            status("idle", text or "Done")
            print(text or response.stop_reason)
            return
        messages.append({"role": "user", "content": results})


if __name__ == "__main__":
    main(" ".join(sys.argv[1:]) or "Find today's weather in Paris.")
