#!/usr/bin/python3
# Jeremy Dilatush - 2020/Nov/28
# Set up net namespaces on a Linux machine for testing oligocast.

# Creates a fixed number of namespaces numbered from 1 up.
# Configures a bridged network among them.

# Names and addresses:
#   on default namespace:
#       bridge interface jjb
#       IPv4 address 10.96.123.254/24
#       IPv6 address fd96:abcd::fe/64
#       MAC address 96:96:96:96:96:FE
#   on namespace jjn$n
#       interface jjl$n (peer jjp$n)
#       IPv4 address 10.96.123.$n/24
#       IPv6 address fd96:abcd::$n/64
#       MAC address 96:96:96:96:96:$n

# Run as root e.g. through sudo.  On Linux.
# Beware, sometimes messing with network config like this can cause the
# machine to lose real network connectivity.  So avoid running it on a machine
# you're using in production, or don't have the opportunity to reset when
# things go wrong.

nsrange = range(1, 6) # 1, 2, 3, 4, 5

from sys import stderr, exit
import glob
from posix import system
from time import sleep

delay_between_commands = lambda: sleep(0.25)
delay_between_phases = lambda: sleep(5.0)

def emit(cmd):
    delay_between_commands()
    print("# "+cmd, flush= True)
    system(cmd)

def msg(str):
    print(str, flush= True, file= stderr)

msg("Removing old jj namespaces and networks")

for n in nsrange:
    emit("ip link delete jjn{:d}".format(n))
    emit("ip link delete jjl{:d}".format(n)) # probably already gone
    emit("ip link delete jjp{:d}".format(n))
emit("ip -all netns delete") # probably already all gone
emit("ip link delete jjb")

delay_between_phases()
msg("Creating namespaces")
for n in nsrange:
    emit("ip netns add jjn{:d}".format(n))

delay_between_phases()
msg("Creating networks")
emit("ip link add jjb type bridge stp_state 0")

delay_between_phases()
for n in nsrange:
    emit("ip link add jjl{:d} type veth peer name jjp{:d}".format(n, n))

delay_between_phases()
for n in nsrange:
    emit("ip link set jjl{:d} address 96:96:96:96:96:{:02x}".format(n, n))
    emit("ip link set jjp{:d} address 96:96:96:96:97:{:02x}".format(n, n))

delay_between_phases()
for n in nsrange:
    emit("ip link set jjl{:d} netns jjn{:d}".format(n, n))
    emit("ip link set jjp{:d} master jjb".format(n))

delay_between_phases()
for n in nsrange:
    emit("ip -n jjn{:d} address add 10.96.123.{:d}/24 dev jjl{:d}".format(n, n, n))
    emit("ip -n jjn{:d} address add fd96:abcd::{:x}/64 dev jjl{:d}".format(n, n, n))

delay_between_phases()
for n in nsrange:
    emit("ip -n jjn{:d} link set jjl{:d} up".format(n, n))
    emit("ip link set jjp{:d} up".format(n))

delay_between_phases()
emit("ip link set jjb address 96:96:96:96:96:fe")
emit("ip address add 10.96.123.254/24 dev jjb")
emit("ip address add fd96:abcd::fe/64 dev jjb")
emit("ip link set jjb up")

delay_between_phases()
