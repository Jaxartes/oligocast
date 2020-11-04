#!/bin/sh
# A script for use in testing "oligocast", in particular for testing
# its handling of -k command input where the boundaries of read() and
# of lines don't match.

iface=wlan0

(
    sleep 2
    printf -- '-E1.1.1.1,2.2.2.2\n-E3.3.3.3,4.4.4.4\n-E5.5.5.5,6.6.6.6'
    sleep 2
    printf -- ',7.7.7.7\n-E-'
    sleep 2
    printf -- '7.7.7.7\n?E\n-E+8.8.8.8,9.9.9.9\n-E+10.10.10.10\n?E\n'
    sleep 2
    printf -- '\n-E+11.11.11.11'
    sleep 2
    printf -- '\n.x\n'
) | ./oligocast -krvi$iface -g225.225.225.225
