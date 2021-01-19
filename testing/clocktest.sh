#!/bin/sh
# testing/clocktest.sh, part of oligocast
# 18 Jan 2020 Jeremy Dilatush
#
# Test how "oligocast" handles clock changes.  That's an uncommon case in
# production use, but it can happen, and it's best for it to be handled
# gracefully.  This script will run 'oligocast' for TX while 
# adjusting the clock.  Then you can look at the timestamps in the output.
# (Testing RX would be nice, but more difficult.  And most of the timing
# logic is the same.)
#
# For this test to work you have to be able to change the clock, and have
# it stay changed.  That may require disabling NTP and/or disabling time sync
# in your virtualization software.

## ## ## some hard coded configuration, you can edit to customize, or not

ifname=lo                   # name of the network interface to use
group=225.2.2.5             # multicast group address to use
opts="-vvv -frawtime"       # options, mainly for formatting
path="./oligocast"          # path to the software under test

## ## ## go for it

sudo -v # make sure we can do root; this may prompt the user for password

(
    echo "..started"
    sleep 30
    for i in 25 50 75 125 150 ; do
        echo "..about to move time forward $i seconds (in 1 second)"
        sleep 1
        sudo date -s "$i sec" >&2
        echo "..moved time forward $i seconds"
        sleep 120
    done
    for i in 25 50 75 125 150 ; do
        echo "..about to move time backward $i seconds (in 1 second)"
        sleep 1
        sudo date -s "$i sec ago" >&2
        echo "..moved time backward $i seconds"
        sleep 120
    done
    echo ".x"
) | "$path" $opts -kti"$ifname" -g$group -P60
