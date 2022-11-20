#!/bin/sh
# testing/data.sh
# A script used in testing "oligocast"

oligocast=./oligocast
iface=wlan0
port=11011
group=226.2.2.6
rxtx=t

if [ "x$1" = "xrx" ] ; then rxtx=r ; fi # option to receive instead of send

(
    sleep 0.5
    for d in \
        len:7 len:30 len:11x hex:124578abde0134679acdf0235689bcef \
        len:22 text:this_is_a_test len:33. len:.33 len:-1 len:44 \
        hex:123456789ABCDE teyt:teyt text:text hex:12345 ; do

        echo "-d$d"
        sleep 3
    done
    echo .x
) | $oligocast -v${rxtx}ki$iface -g$group -p$port
