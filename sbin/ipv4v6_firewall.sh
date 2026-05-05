#!/bin/sh
iptables -F
iptables -X
ip6tables -F
ip6tables -X
iptables -t mangle -I FORWARD 1 -o wan1 -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu
#ipv4
# 1. 部署 INPUT 链规则 (设备本体防护)
iptables -I INPUT 1 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
iptables -I INPUT 2 -i lo -j ACCEPT
iptables -I INPUT 3 -i br0 -j ACCEPT
iptables -I INPUT 4 -i wan1 -p icmp --icmp-type 3 -j ACCEPT
iptables -I INPUT 5 -j DROP
# 2. 部署 FORWARD 链规则 (下挂手机/电脑转发优化)
iptables -I FORWARD 1 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
iptables -I FORWARD 2 -i br0 -o wan1 -j ACCEPT
iptables -I FORWARD 3 -j DROP
# 3. 关上大门：将默认策略设为严格丢弃
iptables -P INPUT DROP
iptables -P FORWARD DROP
#ipv6
ip6tables -I INPUT 1 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
ip6tables -I INPUT 2 -i lo -j ACCEPT
ip6tables -I INPUT 3 -i br0 -j ACCEPT
ip6tables -I INPUT 4 -j DROP
ip6tables -I FORWARD 1 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
ip6tables -I FORWARD 2 -p ipv6-icmp -j ACCEPT
ip6tables -I FORWARD 3 -i br0 -o wan1 -j ACCEPT
ip6tables -I FORWARD 4 -j DROP
ip6tables -P INPUT DROP
ip6tables -P FORWARD DROP
