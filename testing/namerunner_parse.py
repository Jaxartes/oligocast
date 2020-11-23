#!/usr/bin/python3
# namerunner_parse.py
# parses the output of namerunner.c, used in a test for oligocast

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

import re
from sys import stdin, stdout

re_expect   = re.compile("^# expected direction: ([-01]*).* name: \"([^\"]*)\"")
re_usage    = re.compile("^USAGE: ([^ ]*) options[.][.][.]")
re_rxind    = re.compile(".*-m.*multi.*")
re_txind    = re.compile(".*-j.*join.*")
re_abnorm   = re.compile("^#.*exit.*abnormal")
re_exit     = re.compile("^# exit")

st_dir = 0
st_name = "?"
st_rx = False
st_tx = False

cnt_ok = 0
cnt_prob = 0

lines = stdin.read().split("\n")

for line in lines:
    # look for interesting lines
    if len(line) > 0 and line[0] == "#":
        print(line)
    m = re_expect.match(line)
    if m:
        # line tells us what result to expect for the next thing
        st_dir = int(m.group(1))
        st_name = m.group(2)
        if st_name == "!": st_name = "oligocast"
        st_rx = st_tx = False
        continue
    m = re_usage.match(line)
    if m:
        # line shows the command name that got used
        print(line)
        name = m.group(1)
        if name != st_name:
            print("!!! problem with name: exp " +
                  repr(st_name) + " got " + repr(name))
            cnt_prob += 1
        continue
    if re_rxind.match(line):
        # the command is receive capable (indicated by this line from usage())
        print(line)
        st_rx = True
        continue
    if re_txind.match(line):
        # the command in send capable (indicated by this line from usage())
        print(line)
        st_tx = True
        continue
    if re_abnorm.match(line):
        # line shows program terminated abnormally (not "normal" for this
        # test scenario)
        print("!!! problem with exit status")
        cnt_prob += 1
        continue
    if re_exit.match(line):
        # line shows test completed, whether ok or not
        if st_dir <= 0 and not st_rx:
            print("!!! doesn't seem to do receive")
            cnt_prob += 1
            continue
        if st_dir >= 0 and not st_tx:
            print("!!! doesn't seem to do send")
            cnt_prob += 1
            continue
        if st_dir > 0 and st_rx:
            print("!!! seems to do receive")
            cnt_prob += 1
            continue
        if st_dir < 0 and st_tx:
            print("!!! seems to do send")
            cnt_prob += 1
            continue
        # looks ok
        cnt_ok += 1

print("# OK runs: "+repr(cnt_ok))
print("# problems: "+repr(cnt_prob))

