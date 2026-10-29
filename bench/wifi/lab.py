"""The demo desktop on Modal, for bench/wifi: jumpseat streaming Chromium on a
test page (a caret-free textarea for typing, a canvas that animates between F2
and F3, a long body to scroll). Prints `URL TOKEN` of a kept sandbox.

    python3 bench/wifi/lab.py          # start or reuse
    python3 bench/wifi/lab.py stop
    python3 bench/wifi/lab.py sh 'tail /tmp/jumpseat.log'   (the server's log)
    python3 bench/wifi/lab.py env JUMPSEAT_TAIL=3          (restart it with these settings)
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
    if sys.argv[1:2] == ["env"]:  # restart jumpseat with these settings
        with open(STATE) as f: sb = modal.Sandbox.from_id(json.load(f)["id"])
        sb.exec("sh", "-c", f"echo '{' '.join(sys.argv[2:])}' > /tmp/jumpseat.env; pkill -x jumpseat; sleep 1").wait()
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
    # start.sh's desktop, then jumpseat in a loop that reads /tmp/jumpseat.env, so
    # `lab.py env K=V ...` A/Bs a server setting on the same sandbox.
    boot = ("export DISPLAY=:0; Xvfb :0 -screen 0 1280x800x24 -nolisten tcp & sleep 1; openbox & "
            "chromium --no-sandbox --disable-dev-shm-usage --no-first-run --start-maximized --user-data-dir=/tmp/chromium \"$START_URL\" >/dev/null 2>&1 & "
            "touch /tmp/jumpseat.env; while true; do env $(cat /tmp/jumpseat.env) jumpseat --listen :8080 --dir /tmp/jumpseat; sleep 0.2; done")
    sb = modal.Sandbox.create("sh", "-c", boot + " >/tmp/jumpseat.log 2>&1", app=app, image=image, cpu=2, memory=4096,
                              timeout=4 * 3600, encrypted_ports=[8080], region=os.environ.get("WIFI_LAB_REGION", "us-east"),
                              env={"JUMPSEAT_TOKEN": token, "JUMPSEAT_TRACE": "1", "START_URL": "data:text/html," + urllib.parse.quote(PAGE)})
    url = sb.tunnels()[8080].url
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE, "w") as f: json.dump({"id": sb.object_id, "url": url, "token": token}, f)
    print(url, token)

if __name__ == "__main__":
    main()
