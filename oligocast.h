/*
 * oligocast.h
 * Copyright (c) 2020 Jeremy Dilatush
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY JEREMY DILATUSH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL JEREMY DILATUSH OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Interfaces between the main program (oligocast.c) and the compatibility
 * code (oligocast_compat.c).
 */

/* structures */
struct oligocast_if {
    /* 
     * Information to identify a network interface.  Not all fields are filled
     * in on all platforms -- just what's used.
     */
    char            nam[IFNAMSIZ+1];    /* interface name */
    unsigned int    idx;                /* interface index */
    struct in_addr  adr;                /* interface address */
};

/* functions in oligocast.c */

/* functions in oligocast_compat.c */
void identify_interface(char *name, struct oligocast_if *intf,
                        char *errbuf, size_t errlen);
int setup_mcast_listen(int sok, struct oligocast_if *intf,
                       struct sockaddr *group, socklen_t grouplen,
#ifdef DO_SOURCES
                       uint32_t fmode,
                       int numsrc, struct sockaddr_storage *sources,
#endif /* DO_SOURCES */
                       void **arb, int first_time);

