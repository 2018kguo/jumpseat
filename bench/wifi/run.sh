#!/bin/sh
# Run probe.mjs through each wifi profile from inside the jswifi namespace.
#   run.sh URL TOKEN [profiles...] [-- extra probe args]   (netns.sh up first)
url=$1 token=$2; shift 2
profiles=${*:-clean good bad awful lossy jitter}
for p in $profiles; do
  sudo sh "$(dirname "$0")/netns.sh" profile "$p"
  for r in 1 2; do sudo ip netns exec jswifi sudo -u "$(id -un)" node "$(dirname "$0")/probe.mjs" "$url" "$token" "$p"; done
done
sudo sh "$(dirname "$0")/netns.sh" profile clean
