"""The encoder lab on an x86 Modal sandbox, kept between runs (local.sh's
twin; perf does not run under gVisor, so profiles come from callgrind).

    python3 bench/modal_lab.py rd usage=1          BD-rate vs x264
    python3 bench/modal_lab.py run usage=1 qp=17   one point per workload
    python3 bench/modal_lab.py ir qp=17 usage=1     CPU as instructions vs x264
    python3 bench/modal_lab.py callgrind scroll usage=1
    python3 bench/modal_lab.py sh 'any command'

Each call syncs h264/ (jumpseat's encoder) and bench/ into the sandbox with
their mtimes, so make rebuilds only what changed. LAB_SRC=stock measures the
image's stock OpenH264 v2.6.0 instead.
"""
from __future__ import annotations

import io, json, os, subprocess, sys, tarfile

import modal

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DATA = os.environ.get("BENCH_DATA", os.path.join(HERE, "..", "..", "jumpseat-bench-data"))
STATE = os.path.expanduser("~/.cache/jumpseat-lab.json")
MAKE = 'make -C /h264 -j4 -s >/tmp/build.log 2>&1 || { grep -iE "error" /tmp/build.log; exit 1; }'


def sandbox() -> modal.Sandbox:
    try:
        with open(STATE) as f:
            sb = modal.Sandbox.from_id(json.load(f)["id"])
        if sb.poll() is None:
            return sb
    except Exception:
        pass
    image = modal.Image.from_dockerfile(os.path.join(HERE, "Dockerfile"), context_dir=HERE).apt_install("valgrind", "zstd", "python3")
    for name in ("scroll", "anim", "type"):
        image = image.add_local_file(os.path.join(DATA, f"{name}.bgrx.zst"), f"/frames/{name}.bgrx.zst", copy=True)
    app = modal.App.lookup("jumpseat-lab", create_if_missing=True)
    sb = modal.Sandbox.create("sleep", "infinity", app=app, image=image, cpu=4, memory=8192, timeout=3 * 3600)
    exec_(sb, "mkdir -p /data /src && for f in /frames/*.zst; do zstd -q -d $f -o /data/$(basename $f .zst); done")
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE, "w") as f:
        json.dump({"id": sb.object_id}, f)
    return sb


def exec_(sb: modal.Sandbox, command: str, stdin: bytes | None = None, env: dict | None = None) -> int:
    p = sb.exec("sh", "-c", command, env=env)
    if stdin is not None:
        for i in range(0, len(stdin), 1 << 20):  # the client buffers at most a few MB
            p.stdin.write(stdin[i:i + (1 << 20)])
            p.stdin.drain()
        p.stdin.write_eof()
        p.stdin.drain()
    for line in p.stdout:
        print(line, end="", flush=True)
    sys.stderr.write(p.stderr.read())
    return p.wait()


def sync(sb: modal.Sandbox) -> None:
    files = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "h264", "bench"], cwd=REPO,
                           capture_output=True, text=True, check=True).stdout.split()
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for name in files:
            if os.path.exists(os.path.join(REPO, name)):
                tar.add(os.path.join(REPO, name), arcname=name)
    # Extracted with their mtimes, so make rebuilds only what changed; files
    # deleted here are deleted there, or make's wildcards would build them.
    want = "\n".join(sorted(n for n in files if os.path.exists(os.path.join(REPO, n))))
    exec_(sb, "tar -xzf - -C / && cd / && find h264 bench -type f ! -path 'h264/build/*' | sort > /tmp/have"
              " && printf '%s\\n' \"$WANT\" | sort > /tmp/want && comm -23 /tmp/have /tmp/want | xargs -r rm -f",
          stdin=buf.getvalue(), env={"WANT": want})


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    sb = sandbox()
    sync(sb)
    src = "/openh264" if os.environ.get("LAB_SRC") == "stock" else "/h264"
    env = " ".join(f"{k}='{os.environ[k]}'" for k in ("JUMPSEAT_EXP", "WORKLOADS", "X264", "CRF", "X264_NO_AVX512") if k in os.environ)
    build = f"{MAKE} && export OPENH264={src} {env} && "
    cmd, args = sys.argv[1], " ".join(f"'{a}'" for a in sys.argv[2:])
    if cmd == "rd":
        return exec_(sb, build + f"sh /bench/sweep.sh {args} | tee /tmp/sweep.csv | python3 /bench/bdrate.py && echo && cat /tmp/sweep.csv")
    if cmd == "run":
        return exec_(sb, build + f"sh /bench/run.sh {args}")
    if cmd == "ir":
        return exec_(sb, build + f"sh /bench/ir.sh {args}")
    if cmd == "callgrind":
        work, rest = sys.argv[2], " ".join(sys.argv[3:])
        return exec_(sb, build + "sh /bench/run.sh enc=0 >/dev/null 2>&1; "
                     f"valgrind --tool=callgrind --toggle-collect='WelsEnc::CWelsH264SVCEncoder::EncodeFrame(*' --callgrind-out-file=/tmp/cg.out /tmp/encode_bench /data/{work}.bgrx {rest} >/dev/null 2>&1; "
                     # nasm routines have no debug info: name them from the symbol table.
                     "nm -n /tmp/encode_bench | awk '{print $1, $3}' > /tmp/syms; "
                     "callgrind_annotate /tmp/cg.out 2>/dev/null | sed -n '/file:function/,$p' | head -32 | python3 /bench/symbolize.py /tmp/syms")
    if cmd == "sh":
        return exec_(sb, build + " ".join(sys.argv[2:]))
    print(__doc__)
    return 2


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    os._exit(code)  # skips the Modal client's noisy event-loop teardown
