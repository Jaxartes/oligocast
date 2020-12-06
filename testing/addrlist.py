#!/usr/bin/python3
# "addrlist.py" generates comma delimited lists of addresses for
# use in testing "oligocast".
#
# Command line parameters:
#       IP version (4 or 6)
#       number of addresses

from sys import argv, exit, stderr

if len(argv) != 3:
    print("USAGE: python3 testing/addrlist.py ipver numaddrs", file= stderr)
    exit(1)

ipv = int(argv[1])
numaddrs = int(argv[2])

addrs = []

for i in range(numaddrs):
    if ipv == 4:
        addrs.append("10.2.{:d}.{:d}".format((i % 251) + 1,
                                             (i % 241) + 4))
    else:
        addrs.append("fdfd:fdfd::{:x}:{:x}".format((i % 65521) + 1,
                                                   (i % 65519) + 4))

print(",".join(addrs))

