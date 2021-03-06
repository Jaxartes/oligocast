#!/bin/sh
# script to generate some IGMP/MLD activity and try to capture it
# in a file named 'snappo'; it'll take some editing to get it to
# work on your network.

# Before running this: do "sudo -v"
# After this is done: terminate with control-C and copy the resulting file.

iface=wlan0
interval=30
lilint=5
m4base=231.0.0
s4base=232.0.0
u4base=10.0.1
m6base=ff15::abc
s6base=ff35::abc
u6base=fe80::abc
outfile=snappo

doit() {
    ( sleep $interval ; echo ".x") | sudo ./oligocast -ri$iface -g$1 "$2" -k
}

sudo tcpdump -ni$iface -s2048 -w$outfile ip proto 2 or ip6 proto 0 &
tp=$!
echo "tcpdump pid='$tp'"
doit $m4base.1 -E-
doit $m4base.2 -E$u4base.1
doit $m4base.3 -E$u4base.2,$u4base.3
doit $s4base.4 -I-
doit $s4base.5 -I$u4base.4
doit $s4base.6 -I$u4base.5,$u4base.6
doit $m6base:1 -E-
doit $m6base:2 -E$u6base:1
doit $m6base:3 -E$u6base:2,$u6base:3
doit $s6base:4 -I-
doit $s6base:5 -I$u6base:4
doit $s6base:6 -I$u6base:5,$u6base:6

echo "Terminate this program with control-C"
# Somehow "sudo kill $tp" isn't working in this script, which is why 
# I did the control-C thing.
while : ; do sleep 7200 ; done
