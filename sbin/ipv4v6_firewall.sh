#!/bin/sh
iptables -F
iptables -X
ip6tables -F
ip6tables -X
iptables -t mangle -A FORWARD -o wan1 -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu
#ipv4
iptables -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
iptables -A INPUT -i lo -j ACCEPT
iptables -A INPUT -i br0 -j ACCEPT
iptables -A INPUT -i wan1 -p icmp --icmp-type 3 -j ACCEPT
iptables -A INPUT -j DROP

iptables -A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
iptables -A FORWARD -i br0 -o wan1 -j ACCEPT
iptables -A FORWARD -j DROP

iptables -P INPUT DROP
iptables -P FORWARD DROP
#ipv6
ip6tables -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
ip6tables -A INPUT -i lo -j ACCEPT
ip6tables -A INPUT -i br0 -j ACCEPT
ip6tables -A INPUT -j DROP

ip6tables -A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
ip6tables -A FORWARD -p ipv6-icmp -j ACCEPT
ip6tables -A FORWARD -i br0 -o wan1 -j ACCEPT
ip6tables -A FORWARD -j DROP

ip6tables -P INPUT DROP
ip6tables -P FORWARD DROP
