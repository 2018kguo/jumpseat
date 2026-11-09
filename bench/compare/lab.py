"""jumpseat against noVNC (x11vnc + websockify) on one Modal sandbox: the demo
desktop with bench/compare/page.html open, and one server at a time on :8080.
probe.mjs watches it from a real browser across the network.

    python3 bench/compare/lab.py                 # start or reuse; prints URL TOKEN
    python3 bench/compare/lab.py serve jumpseat  # or novnc: switch the server
    python3 bench/compare/lab.py page scroll     # or anim, type: the workload
    python3 bench/compare/lab.py cpu             # utime+stime ticks per process name
    python3 bench/compare/lab.py sh 'cmd'        # run a command on the sandbox
    python3 bench/compare/lab.py stop
"""
import json, os, secrets, sys
import modal

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
STATE = os.path.expanduser("~/.cache/jumpseat-compare-lab.json")
SERVERS = {
    "jumpseat": "jumpseat --listen :8080 --dir /tmp/jumpseat",
    # x11vnc as noVNC's own docs run it; noVNC's client served by websockify.
    "novnc": "x11vnc -display :0 -forever -shared -nopw -quiet -rfbport 5900 & "
             "websockify --web /usr/share/novnc 8080 localhost:5900",
}


def sandbox():
    with open(STATE) as f:
        return modal.Sandbox.from_id(json.load(f)["id"])


def sh(sb, command):
    p = sb.exec("sh", "-c", command)
    p.wait()
    return p.stdout.read()


def main():
    cmd = sys.argv[1:2]
    if cmd == ["stop"]:
        sandbox().terminate()
        os.remove(STATE)
        return
    if cmd == ["serve"]:
        sb = sandbox()
        # The boot loop runs whichever server /tmp/server names.
        sh(sb, f"echo {sys.argv[2]} > /tmp/server; pkill -x jumpseat; pkill -x x11vnc; pkill -x websockify; sleep 6")
        return
    if cmd == ["page"]:
        # The boot loop restarts Chromium on whatever /tmp/page names.
        sh(sandbox(), f"echo 'file:///bench/page.html#{sys.argv[2]}' > /tmp/page; pkill -x chromium; sleep 5")
        return
    if cmd == ["sh"]:
        print(sh(sandbox(), sys.argv[2]), end="")
        return
    if cmd == ["cpu"]:
        print(sh(sandbox(), "for d in /proc/[0-9]*; do read -r s < $d/stat 2>/dev/null || continue; "
                            "c=${s#*(}; c=${c%%)*}; r=${s##*) }; set -- $r; echo \"$c $(( ${12} + ${13} ))\"; done"), end="")
        return
    try:
        with open(STATE) as f:
            st = json.load(f)
        if modal.Sandbox.from_id(st["id"]).poll() is None:
            print(st["url"], st["token"])
            return
    except Exception:
        pass
    token = secrets.token_hex(16)
    image = (modal.Image.from_dockerfile(os.path.join(REPO, "docker/Dockerfile"), context_dir=REPO,
                                         ignore=["h264/build", "**/node_modules", ".git", ".bench"])
             .apt_install("x11vnc", "novnc", "websockify")
             .add_local_file(os.path.join(HERE, "page.html"), "/bench/page.html"))
    app = modal.App.lookup("jumpseat-compare-lab", create_if_missing=True)
    boot = (f"echo {token} > /tmp/token; export DISPLAY=:0; Xvfb :0 -screen 0 1280x800x24 -nolisten tcp & sleep 1; openbox & "
            "echo file:///bench/page.html#type > /tmp/page; "
            "while true; do rm -rf /tmp/chromium; chromium --no-sandbox --test-type --disable-dev-shm-usage --no-first-run --start-maximized "
            "--disable-features=ChromeWhatsNewUI,SigninInterceptBubble --user-data-dir=/tmp/chromium \"$(cat /tmp/page)\" "
            ">/dev/null 2>&1; sleep 0.5; done & echo jumpseat > /tmp/server; "
            "while true; do case $(cat /tmp/server) in "
            f"jumpseat) JUMPSEAT_TOKEN={token} {SERVERS['jumpseat']} ;; "
            f"novnc) {SERVERS['novnc']} ; pkill -x x11vnc ;; esac >>/tmp/server.log 2>&1; sleep 0.5; done")
    # The same machine size as the README's numbers: 4 vCPU, gVisor, no GPU.
    sb = modal.Sandbox.create("sh", "-c", boot, app=app, image=image, cpu=4, memory=8192, timeout=4 * 3600,
                              encrypted_ports=[8080], region=os.environ.get("COMPARE_LAB_REGION", "us-east"))
    url = sb.tunnels()[8080].url
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE, "w") as f:
        json.dump({"id": sb.object_id, "url": url, "token": token}, f)
    print(url, token)


if __name__ == "__main__":
    main()
