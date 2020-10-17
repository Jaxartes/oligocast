#!/bin/sh
# script to generate some IGMP/MLD activity and try to capture it
# in a file named 'snappo'; it'll take some editing to get it to
# work on your network.

# Before running this: do "sudo -v"
# After running this: terminate with control-C and grab the resulting file

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
    grp=$1 ; shift
    mo=$1 ; shift
    il=$1 ; shift
    (
        for lst in $* ; do
            sleep $interval
            echo $mo$lst
        done
        sleep $interval
        echo .x
    ) | sudo ./oligocast -ri$iface -g$grp $mo$il -k
}

sudo tcpdump -ni$iface -s2048 -w$outfile ip proto 2 or ip6 proto 0 &
tp=$!
echo "tcpdump pid='$tp'"

doit $m4base.7 -E - $u4base.1 $u4base.1,$u4base.2 $u4base.3 - $u4base.7
doit $s4base.8 -I - $u4base.4 $u4base.4,$u4base.5 $u4base.6 - $u4base.8
doit $m6base:7 -E - $u6base:1 $u6base:1,$u6base:2 $u6base:3 - $u6base:7
doit $s6base:8 -I - $u6base:4 $u6base:4,$u6base:5 $u6base:6 - $u6base:8

echo "Terminate this program with control-C"
# Somehow "sudo kill $tp" isn't working in a similar script, which is why 
# I did the control-C thing.
while : ; do sleep 7200 ; done

