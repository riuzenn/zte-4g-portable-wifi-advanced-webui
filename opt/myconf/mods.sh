#!/bin/sh
sysctl -qp /opt/myconf/sysctl.conf
sleep 15
/opt/myconf/ipv4v6_firewall.sh
echo 0 > /sys/class/leds/modem_w_led/brightness
mount --bind /opt/myconf/resolv.conf /etc/resolv.conf
killall dnsmasq
