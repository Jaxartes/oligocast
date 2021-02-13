# Oligocast

## Introduction

Welcome to "oligocast," a test tool for multicast networking.  Oligocast
can send and receive UDP packets on multicast groups.  It's similar to
"mtools", but with some enhancements:
* source filtering (including source specific multicast)
* IPv4 and IPv6 in the same program
* send and receive from the same program (by default)

It's meant for use from the Linux command line and similar environments.

## Status

Oligocast is open source, free to use and redistribute under a BSD-type license.

At this time, oligocast hasn't been much tested; use with care.  It's
been tested for basic functioning on Linux; and even less on macOS; and
hasn't been tested at all on *BSD.

## Using

To use oligocast:
* Get the sources: `git clone https://github.com/Jaxartes/oligocast`
* If you're building for an unusual platform you may have to edit
  the file "oligocast_config.h".  On recent Linux you shouldn't need to.
* Build with "make".
* To send packets: Use the "-t" option.
* To receive packets: Use the "-r" option.
* In both cases, also use "-g" to specify the multicast group and
  "-i" for the network interface name.

Examples:
* `oligocast -tiwlan0 -g224.2.2.2`
* `oligocast -r -iwlan0 -gff15::33`
* `oligocast -rviotn3v1 -le32 -gff15::ff -Efd96:1::3,fd96:1::2`

## Other comments

What's in the name? It's a play on words.  In "multicast", "multi-"
means "many."  "Oligo-" means "few."

If you run "oligocast" as "oligosend" (perhaps through a symlink) it will
only send and you won't need the "-t" option.  Likewise "oligorecv".

Source filtering is specified with the "-I" and "-E" options.  It can
be changed on the fly, by using the "-k" option and then passing new
"-I" and "-E" on standard input.

## TODO

There is much to be done on this project:
* Test on non-Linux platforms.
* Write a manual page (manpage) for this program.
* Once a manual page is written, trim down `usage()`, which is way too long,
  down to just a list of options.
* Go through the code for places `errthrottle()` should be used.
* Maybe #ifdef all the IPv6-specific code, for building on systems
  where IPv6 multicast is missing or badly broken.  Maybe one for IPv4 too.
* Potential feature: When receiving a packet, report the source address.
* Potential feature: When receiving a packet, optionally
  check its contents against "-d".
* Potential feature: Make "-i" optional, having it pick an interface for
  itself if none is specified.
