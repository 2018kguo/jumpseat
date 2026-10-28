"""Names callgrind_annotate's ???:0xADDR rows from `nm -n` output (argv[1])."""
import bisect, re, sys

syms = []
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) == 2:
        try:
            syms.append((int(parts[0], 16), parts[1]))
        except ValueError:
            pass
syms.sort()
addrs = [a for a, _ in syms]
for line in sys.stdin:
    m = re.search(r"\?\?\?:0x([0-9a-f]+) \[/tmp/encode_bench\]", line)
    if m:
        i = bisect.bisect_right(addrs, int(m.group(1), 16)) - 1
        if i >= 0:
            line = line.replace(m.group(0), "asm:" + syms[i][1])
    line = line.replace(" [/tmp/encode_bench]", "")
    line = re.sub(r"(\w)\((?:[^()]|\([^()]*\))*\)$", r"\1", line.rstrip())  # drop C++ argument lists
    line = re.sub(r"/src/(\./)?codec/", "", line)
    print(line.rstrip()[:150])
