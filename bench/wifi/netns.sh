#!/bin/sh
# An emulated wifi link for bench/wifi, isolated in a network namespace
# (jswifi) so nothing else on the host is shaped. Needs root (sudo).
#   netns.sh up                 namespace, veth pair, NAT for 10.213.0.0/30 only
#   netns.sh profile NAME       apply a link profile (both directions)
#   netns.sh down               remove it all and restore ip_forward
# Profiles: one-way delay, jitter and loss each way, on a 30 Mbit/s link.
set -e
NS=jswifi; H=jsw0; C=jsw1; NET=10.213.0.0/30
STATE=/run/jswifi.ip_forward
case "$1" in
up)
  ip netns add $NS
  ip link add $H type veth peer name $C
  ip link set $C netns $NS
  ip addr add 10.213.0.1/30 dev $H && ip link set $H up
  ip netns exec $NS sh -c "ip addr add 10.213.0.2/30 dev $C && ip link set $C up && ip link set lo up && ip route add default via 10.213.0.1"
  mkdir -p /etc/netns/$NS && echo "nameserver 1.1.1.1" > /etc/netns/$NS/resolv.conf
  sysctl -n net.ipv4.ip_forward > $STATE && sysctl -qw net.ipv4.ip_forward=1
  iptables -I FORWARD 1 -i $H -s $NET -j ACCEPT
  iptables -I FORWARD 1 -o $H -d $NET -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
  iptables -t nat -A POSTROUTING -s $NET ! -o $H -j MASQUERADE
  ;;
profile)
  case "$2" in
    clean)  args="" ;;
    good)   args="delay 10ms 5ms distribution normal loss 0.2%" ;;
    bad)    args="delay 25ms 20ms distribution normal loss 1%" ;;
    awful)  args="delay 50ms 40ms distribution normal loss 3%" ;;
    lossy)  args="delay 10ms 5ms distribution normal loss 3%" ;;
    jitter) args="delay 30ms 30ms distribution normal" ;;
    *) echo "unknown profile $2" >&2; exit 2 ;;
  esac
  # rate keeps packets in order (netem reorders jittered packets without it)
  tc qdisc replace dev $H root netem rate 30mbit $args limit 10000
  ip netns exec $NS tc qdisc replace dev $C root netem rate 30mbit $args limit 10000
  ;;
down)
  iptables -D FORWARD -i $H -s $NET -j ACCEPT 2>/dev/null || true
  iptables -D FORWARD -o $H -d $NET -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT 2>/dev/null || true
  iptables -t nat -D POSTROUTING -s $NET ! -o $H -j MASQUERADE 2>/dev/null || true
  [ -f $STATE ] && sysctl -qw net.ipv4.ip_forward=$(cat $STATE) && rm -f $STATE
  ip link del $H 2>/dev/null || true
  ip netns del $NS 2>/dev/null || true
  rm -rf /etc/netns/$NS
  ;;
*) sed -n 2,9p "$0"; exit 2 ;;
esac
