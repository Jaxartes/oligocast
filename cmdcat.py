#!/usr/bin/python3
# cmdcat.py
# Takes the following simple commands on stdin, one per line:
#       #comment
#           comments and blank lines are ignored
#       <filename
#           copy the contents of file named 'filename' to stdin
#       number<filename
#           copy the contents of the file named 'filename' to stdin
#           that many times
#       >string
#           write the specified line to stdin
#       number>string
#           write the specified line to stdin the specified number of times
#       _sec
#           delay the specified number of seconds
#       exit
#           terminate the script
# Created for use in testing 'oligocast'.

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
from sys import stderr, stdin, stdout

fails = 0
while True:
    # get a line of input
    line = input()

    # Recognize commands, comments, and blank lines.  This
    # script does not strip whitespace out of the input.
    try:
        if len(line) == 0:
            # blank line, ignore
            pass
        elif line[0] == '#':
            # comment, ignore
            pass
        elif line[0] == '_':
            # counted delay
            sleep(float(line[1:]))
        elif line == "exit":
            # end the script
            exit()
        elif line.find('<') >= 0:
            # read file
            lt = line.find('<')
            if lt > 0:
                count = int(line[:lt])
            else:
                count = 1
            while count > 0:
                count -= 1
                fp = open(line[(lt+1):], 'r')
                while True:
                    get = 32768
                    buf = fp.read(get)
                    stdout.write(buf)
                    if len(buf) < get: break # end of file
                fp.close()
            stdout.flush()
        elif line.find('>') >= 0:
            # echo line
            gt = line.find('>')
            if gt > 0:
                count = int(line[:gt])
            else:
                count = 1
            while count > 0:
                count -= 1
                stdout.write(line[(gt+1):])
                stdout.write("\n")
            stdout.flush()
        else:
            # unrecognized command, let the user know
            raise Exception("Invalid command input to cmdcat.py")
        fails = 0
    except Exception as e:
        print(str(e), file=stderr)
        fails += 1
        if fails > 100:
            print("Too many failures, terminating", file=stderr)
            exit()
