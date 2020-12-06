#!/usr/bin/python3
# "deltatest2.py" test input generator for oligocast
# Generates two output streams:
#       stdout: pipe to ./oligocast -krvilo -fnotime -lx -gff15::abcd
#                    or ./oligocast -krvilo -fnotime -lx -g225.1.1.1
#       stderr: compare to the output of oligocast
# Command line parameters:
#       pseudorandom number generation seed
#       IP version (4 or 6)
#       max number of source addresses
#       number of operations

# Copyright (c) 2020 Jeremy Dilatush
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY JEREMY DILATUSH AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL JEREMY DILATUSH OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

from time import sleep
from sys import stderr, stdin, stdout, argv, exit
import random as rnd
from socket import inet_ntop, AF_INET, AF_INET6

## ## ## read the command line parameters

if len(argv) != 5:
    print("USAGE: python3 testing/deltatest2.py seed ipver addrmax numops\n" +
          "EXAMPLE: python3 testing/deltatest2.py 123 4 10 25\n",
          file= stderr)
    exit(1)

seed = int(argv[1])
ipver = int(argv[2])
addrmax = int(argv[3])
numops = int(argv[4])
rnd.seed(seed)
if ipver !=4 and ipver != 6:
    raise Exception("ipver must be 4 or 6")
if addrmax < 1:
    raise Exception("addrmax must be positive")
if numops < 1:
    raise Exception("numops must be positive")

## ## ## utility functions

def addrout(a):
    """Output address a, represented in plain numeric form."""
    parts = []
    if ipver == 4:
        # IPv4
        for i in range(24, -8, -8):
            parts.append((a >> i) & 255)
        return(inet_ntop(AF_INET, bytes(parts)))
    else:
        # IPv6
        for i in range(120, -8, -8):
            parts.append((a >> i) & 255)
        return(inet_ntop(AF_INET6, bytes(parts)))

def addrsout(a_s):
    """Output a list of addresses."""
    if len(a_s): return(",".join(map(addrout, a_s)))
    return("-")

def mkbits(l, d):
    """Make a number that's a vector of l bits, of which l*d on average
    will be 1."""
    b = 0
    for i in range(l):
        if rnd.uniform(0, 1) < d:
            b += 1 << i
    return (b)

def mkaddr(b, d):
    """Make a unicast address based on b, with a modification density d."""
    if ipver == 4:
        # IPv4: in 10.0.0.0/8
        if b is None: b = 0x0a000000
        return(b ^ mkbits(24, d))
    else:
        # IPv6: in fd05:aaaa::/32
        if b is None: b = 0xfd05aaaa000000000000000000000000
        return(b ^ mkbits(96, d))

## ## ## Internal storage of our own copy of the source address list

sources = []

## ## ## Go for it

if ipver == 4:
    modden = 0.2
else:
    modden = 0.05

o = 0
a = None
while o < numops:
    # figure out what we're going to do; fill in:
    #   new 'sources'
    #   'delta' source list
    #   delta type modifier string 'deltype'
    if o == 0 or rnd.uniform(0, 1) < 0.2:
        # do an absolute set: -Ea,b,c
        deltype = ""
        delta = []
        lencrit = lambda: rnd.randrange(0, addrmax)
        while (len(delta) < lencrit() or
               len(delta) < lencrit()):
            if len(delta) and rnd.uniform(0, 1) < 0.4:
                a = rnd.choice(delta)
            else:
                a = mkaddr(a, rnd.uniform(0, modden))
            delta.append(a)
        sources = sorted(delta)
    elif rnd.choice((False, True)):
        # do a subtractive delta: -E-a,b,c
        deltype = "-"
        delta = []
        lencrit = lambda: rnd.randrange(0,
                                        len(sources) + (len(sources) >> 2) + 1)
        while (len(delta) < lencrit() or
               len(delta) < lencrit()):
            if len(sources) and rnd.uniform(0, 1) < 0.6:
                a = rnd.choice(sources)
            else:
                a = mkaddr(a, rnd.uniform(0, modden))
            delta.append(a)

        if len(delta) == 0: continue # can't have -E--

        # figure out new sources list
        dx = set()
        for s in delta:
            dx.add(s)
        sources2 = []
        for s in sources:
            if s not in dx:
                sources2.append(s)
        sources = sources2
    else:
        # do an additive delta: -E+a,b,c

        deltype = "+"
        delta = []
        lencrit = lambda: rnd.randrange(0, addrmax)
        while (len(delta) < lencrit() or
               len(delta) < lencrit()):
            if len(sources) and rnd.uniform(0, 1) < 0.4:
                a = rnd.choice(sources)
            else:
                a = mkaddr(a, rnd.uniform(0, modden))
            delta.append(a)

        # figure out new sources list - without duplicates
        a_s = set()
        for s in sources:
            a_s.add(s)
        for s in delta:
            a_s.add(s)

        if len(a_s) >= addrmax: continue # don't overflow the address list
        if len(delta) == 0: continue # can't have -E+-

        sources = sorted(a_s)

    # emit commands and their expected output
    print("-E" + deltype + addrsout(delta), file= stdout)
    print("received command for x -E" + deltype + addrsout(delta), file= stderr)
    print("?E", file= stdout)
    print("received command for x ?E", file= stderr)
    print("note: x source setting: -E" + addrsout(sources), file= stderr)

    # count it
    o += 1

## ## ## Finish up

print(".x", file= stdout)
print("received command for x .x", file= stderr)
#print("exiting on command", file= stderr)

exit(0)

