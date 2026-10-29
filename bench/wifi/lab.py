"""The demo desktop on Modal, for bench/wifi: jumpseat streaming Chromium on a
test page (a caret-free textarea for typing, a canvas that animates between F2
and F3, a long body to scroll). Prints `URL TOKEN` of a kept sandbox.

    python3 bench/wifi/lab.py          # start or reuse
    python3 bench/wifi/lab.py stop
    python3 bench/wifi/lab.py sh 'tail /tmp/jumpseat.log'   (the server's log)
"""
import json, os, secrets, sys, urllib.parse
import modal

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STATE = os.path.expanduser("~/.cache/jumpseat-wifi-lab.json")
PAGE = """<!doctype html><body style="margin:0;font:20px monospace">
<textarea id=t autofocus style="width:900px;height:160px;font:20px monospace;caret-color:transparent"></textarea>
<canvas id=b width=480 height=270 style="position:absolute;left:0;top:180px"></canvas>
<div style="height:6000px;padding-top:420px">""" + "".join(f"<p>line {i}: the quick brown fox jumps over the lazy dog</p>" for i in range(150)) + """</div>
<script>let on=false;addEventListener('keydown',e=>{if(e.key==='F2'||e.key==='F3'){on=e.key==='F2';e.preventDefault()}});
const c=b.getContext('2d');c.font='18px monospace';
(function f(t){if(on){c.fillStyle='#fff';c.fillRect(0,0,480,270);for(let i=0;i<5;i++){c.fillStyle='hsl('+((t/8+i*20)%360)+',70%,40%)';c.fillText('frame '+Math.round(t)+' row '+i+' lorem ipsum dolor sit amet',(t/6+i*37)%120,20+i*19)}}requestAnimationFrame(f)})()</script>"""

def main():
    if sys.argv[1:2] == ["sh"]:
        with open(STATE) as f: sb = modal.Sandbox.from_id(json.load(f)["id"])
        p = sb.exec("sh", "-c", sys.argv[2]); print(p.stdout.read(), end=""); print(p.stderr.read(), end="", file=sys.stderr)
        return
    if sys.argv[1:] == ["stop"]:
        with open(STATE) as f: modal.Sandbox.from_id(json.load(f)["id"]).terminate()
        os.remove(STATE); return
    try:
        with open(STATE) as f: st = json.load(f)
        sb = modal.Sandbox.from_id(st["id"])
        if sb.poll() is None:
            print(st["url"], st["token"]); return
    except Exception:
        pass
    token = secrets.token_hex(16)
    image = modal.Image.from_dockerfile(os.path.join(REPO, "docker/Dockerfile"), context_dir=REPO,
                                        ignore=["h264/build", "**/node_modules", ".git", ".bench"])
    app = modal.App.lookup("jumpseat-wifi-lab", create_if_missing=True)
    sb = modal.Sandbox.create("sh", "-c", "/usr/local/bin/start.sh >/tmp/jumpseat.log 2>&1", app=app, image=image, cpu=2, memory=4096,
                              timeout=4 * 3600, encrypted_ports=[8080], region=os.environ.get("WIFI_LAB_REGION", "us-east"),
                              env={"JUMPSEAT_TOKEN": token, "JUMPSEAT_TRACE": "1", "START_URL": "data:text/html," + urllib.parse.quote(PAGE)})
    url = sb.tunnels()[8080].url
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE, "w") as f: json.dump({"id": sb.object_id, "url": url, "token": token}, f)
    print(url, token)

if __name__ == "__main__":
    main()
