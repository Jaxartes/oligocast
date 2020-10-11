#!/usr/bin/python3
# Jeremy Dilatush - 2020/Sep/30
# Set up net namespaces on a Linux machine for testing oligocast.

# Takes two parameters $nodes and $nets on the command line.
# Configures namespaces for $x from 1 to $nodes named otn$x.
# Configures bridged networks for $y from 1 to $nets:
#       IPv4 address of otn$x is 10.96.$y.$x/24
#       IPv6 address of otn$x is fd96:$y::$x/64
#       MAC address of otn$x is 96:96:96:96:$y:$x
#       veth interface on otn$x is otn${x}v$y; other end otn${x}v${y}be
#       bridge interface is otnb$y

# Run as root e.g. through sudo.  On Linux.
# Beware, sometimes messing with network config like this can cause the
# machine to lose real network connectivity.  So avoid running it on a machine
# you're using in production, or don't have the opportunity to reset when
# things go wrong.

from sys import stderr, argv, exit
import glob
from posix import system
from time import sleep

delay_between_commands = lambda: sleep(0.25)
delay_between_networks = lambda: sleep(0.5)
delay_between_phases = lambda: sleep(5.0)

def emit(cmd):
    delay_between_commands()
    print("# "+cmd, flush= True)
    system(cmd)

def msg(str):
    print(str, flush= True, file= stderr)

if len(argv) != 3:
    msg("USAGE: python3 ns-test-setup.py $nodes $nets")
    exit(1)
nodes = int(argv[1])
nets = int(argv[2])

msg("Removing any existing otn namespaces and networks")

emit("ip -all netns delete")
npfx = "/proc/sys/net/ipv4/conf/"
for dobe in (False, True):
    for netpath in glob.glob(npfx+"otn*"):
        net = netpath[len(npfx):]
        if dobe or net[-2:] != "be":
            emit("ip link delete "+net)

delay_between_phases()
msg("Creating namespaces")
for node in range(1, nodes + 1):
    emit("ip netns add otn{:d}".format(node))

delay_between_phases()
msg("Creating networks")
for net in range(1, nets + 1):
    emit("ip link add otnb{:d} type bridge stp_state 0".
                format(net))
    delay_between_networks()
delay_between_phases()
for net in range(1, nets + 1):
    for node in range(1, nodes + 1):
        emit("ip link add otn{:d}v{:d} type veth peer name otn{:d}v{:d}be".
                    format(node, net, node, net))
    delay_between_networks()
delay_between_phases()
for net in range(1, nets + 1):
    for node in range(1, nodes + 1):
        emit("ip link set otn{:d}v{:d} address 96:96:96:96:{:02x}:{:02x}".
                    format(node, net, net, node))
        emit("ip link set otn{:d}v{:d}be address 96:96:96:97:{:02x}:{:02x}".
                    format(node, net, net, node))
    delay_between_networks()
delay_between_phases()
for net in range(1, nets + 1):
    for node in range(1, nodes + 1):
        emit("ip address add 10.96.{:d}.{:d}/24 dev otn{:d}v{:d}".
                    format(net, node, node, net))
        emit("ip address add fd96:{:x}::{:x}/64 dev otn{:d}v{:d}".
                    format(net, node, node, net))
    delay_between_networks()
delay_between_phases()
for net in range(1, nets + 1):
    for node in range(1, nodes + 1):
        emit("ip link set otn{:d}v{:d} netns otn{:d}".
                    format(node, net, node))
        emit("ip link set otn{:d}v{:d}be master otnb{:d}".
                    format(node, net, net))
    delay_between_networks()
delay_between_phases()
for net in range(1, nets + 1):
    for node in range(1, nodes + 1):
        emit("ip -n otn{:d} address add 10.96.{:d}.{:d}/24 dev otn{:d}v{:d}".
                    format(node, net, node, node, net))
        emit("ip -n otn{:d} address add fd96:{:x}::{:x}/64 dev otn{:d}v{:d}".
                    format(node, net, node, node, net))
    delay_between_networks()
delay_between_phases()
for net in range(1, nets + 1):
    for node in range(1, nodes + 1):
        emit("ip -n otn{:d} link set otn{:d}v{:d} up".
                    format(node, node, net))
        emit("ip link set otn{:d}v{:d}be up".
                    format(node, net))
    emit("ip link set otnb{:d} up".
                format(net))
    delay_between_networks()
