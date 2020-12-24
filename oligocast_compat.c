/*
 * oligocast_compat.c
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
 * oligocast_compat.c:
 *
 * Compatibility code for "oligocast".  See "oligocast.c" for the main
 * thing.  See "oligocast_compat.h" for compatibility settings.  Where
 * rather different code is needed on different platforms to accomplish
 * the same thing, it goes here.
 *
 * Some of the APIs used for multicast end up not being quite portable.
 * It's actually worse in the case of IPv4.  Many of the problems involve
 * identifying particular network interfaces, which you have to do with
 * multicast.  For IPv6, standard APIs take an "interface index" and provide
 * a way to look it up.  For IPv4, some implementations also use the
 * interface index, while many others rely on the interface address and
 * it can be difficult to find that out.
 *
 * 
 */

#include "oligocast_config.h"
#if defined(__linux__) && defined(HAVE_SETSOURCEFILTER)
#define _GNU_SOURCE /* for setsourcefilter() */
#endif 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if.h>
#ifdef HAVE_GETIFADDRS
#include <ifaddrs.h>
#endif
#include "oligocast.h"

#if !defined(HAVE_MULTICAST_IF_IP_MREQN)
/* want interface address to use in 'IP_MULTICAST_IF' socket option */
#define WANT_IFADDR
#endif

/*
 * setup_mcast_listen()
 *
 * Set a socket up for listening to a multicast group.
 * Implemented using setsourcefilter() (RFC 3678), where available,
 * and the API is similar.
 *
 * Parameters:
 *      sok -- the socket to act on
 *      intf -- identifies the network interface it's to listen on
 *      group -- multicast group address to join/filter
 *      grouplen -- length of *group
 *      fmode -- filter mode MCAST_INCLUDE or MCAST_EXCLUDE
 *      numsrc -- number of source addresses in slist[]
 *      slist -- source addresses to include/exclude depending on fmode
 *      state -- A structure that holds any data that setup_mcast_listen()
 *          needs to retain from one call to another.  The caller will
 *          allocate it and keep it around, but won't do anything else except
 *          initialize 'state->ever_called' to 0.
 * Return value:
 *      >=0 on success
 *      <0 on error, and errno is set
 */
int setup_mcast_listen(int sok, struct oligocast_if *intf,
                       struct sockaddr *group, socklen_t grouplen,
#ifdef DO_SOURCES
                       uint32_t fmode,
                       int numsrc, struct sockaddr_storage *sources,
#endif
                       struct oligocast_sml_state *state)
{
    int rv = 0;

    if (!state->ever_called) {
        /* initialize the state structure */
        state->ever_called = 1;
        state->joined = 0;
    }

#ifdef DO_SOURCES
    if (fmode == MCAST_INCLUDE && numsrc == 0 && !state->joined) {
        /* we haven't joined the group and don't want to */
        return(0);
    }
#else /* DO_SOURCES */
    if (state->joined) {
        /* joining is all we want to do and we've done it */
        return(0);
    }
#endif /* !DO_SOURCES */
#if !defined(DO_SOURCES) || defined(NEED_MEMBERSHIP_FIRST)
    if (!state->joined) {
        /* 
         * Use a socket option to join the group. What's available for this?
         *      For IPv4:
         *          IP_ADD_MEMBERSHIP which appears in RFC 3678, Linux, BSD.
         *      For IPv6: MCAST_JOIN_GROUP?
         *          IPV6_JOIN_GROUP which appears in RFC 3493, BSD. Linux?
         *          IPV6_ADD_MEMBERSHIP which appears in Linux.
         *      For IPv4 or IPv6:
         *          MCAST_JOIN_GROUP which appears in RFC 3678.
         */
        if (group->sa_family == AF_INET6) {
            /* IPv6 */
            struct ipv6_mreq ipv6mr;
            memset(&ipv6mr, 0, sizeof(ipv6mr));
            ipv6mr.ipv6mr_multiaddr = ((struct sockaddr_in6 *)group)->sin6_addr;
            ipv6mr.ipv6mr_interface = intf->idx;
            rv = setsockopt(sok, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                            &ipv6mr, sizeof(ipv6mr));
            if (rv) { return(rv); }
        } else {
            /* IPv4 */
#ifdef HAVE_IP_ADD_MEMBERSHIP_IP_MREQN
            struct ip_mreqn imr;
            memset(&imr, 0, sizeof(imr));
            imr.imr_multiaddr = ((struct sockaddr_in *)group)->sin_addr;
            imr.imr_address.s_addr = INADDR_ANY;
            imr.imr_ifindex = intf->idx;
#else /* HAVE_IP_ADD_MEMBERSHIP_IP_MREQN */
            struct ip_mreq imr;
            memset(&imr, 0, sizeof(imr));
            imr.imr_multiaddr = ((struct sockaddr_in *)group)->sin_addr;
            imr.imr_interface = intf->adr;
#ifndef WANT_IFADDR
#define WANT_IFADDR /* make identify_interface() fill in intf->adr for us */
#endif
#endif /* !HAVE_IP_ADD_MEMBERSHIP_IP_MREQN */
            rv = setsockopt(sok, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                            &imr, sizeof(imr));
            if (rv) { return(rv); }
        }
        state->joined = 1;
    }
#endif /* DO_SOURCES || NEED_MEMBERSHIP_FIRST */
#ifdef DO_SOURCES
#ifdef HAVE_SETSOURCEFILTER
    /*
     * Can use setsourcefilter() to set up source specific multicast.
     * If it's present and works.
     */
    rv = setsourcefilter(sok, intf ? intf->idx : 0, group, grouplen,
                         fmode, numsrc, sources);
    if (rv) { return(rv); }
#else /* HAVE_SETSOURCEFILTER */
    /*
     * Source specific multicast without setsourcefilter(): Can definitely
     * be done, but isn't done here yet.
     */
#error "not implemented"
#endif /* !HAVE_SETSOURCEFILTER */
    if (fmode == MCAST_INCLUDE && numsrc == 0) {
        /*
         * At least on Linux, this causes us to leave the group.  So
         * remember to rejoin it.
         */
        state->joined = 0;
    }
#endif /* !DO_SOURCES */

    return(0);
}

/*
 * identify_interface()
 *
 * Get what information we're going to need about an interface.
 *
 * Parameters:
 *      name -- name of interface (input)
 *      intf -- place to put the info we got (output)
 *      errbuf -- filled in with error message if any, empty string otherwise
 *      errlen -- length of errbuf in bytes
 */
void identify_interface(char *name, struct oligocast_if *intf,
                        char *errbuf, size_t errlen)
{
    /* initialize the output fields */
    memset(intf, 0, sizeof(*intf));
    errbuf[0] = '\0';

    /* sanity checks */
    if (name == NULL || name[0] == '\0') {
        snprintf(errbuf, errlen, "missing interface name");
        return;
    }
    if (strlen(name) >= sizeof(intf->nam)) {
        snprintf(errbuf, errlen, "interface name too long");
        return;
    }

    /* interface name is always available and sometimes useful */
    strcpy(intf->nam, name); /* array can't overflow: checked above */

    /*
     * Interface index is usually available (RFC 2553 defines
     * if_nametoindex()) and usually useful.
     */
    intf->idx = if_nametoindex(intf->nam);
    if (intf->idx == 0) {
        snprintf(errbuf, errlen, "interface '%s' error: %s",
                 intf->nam, strerror(errno));
        return;
    }

#ifdef WANT_IFADDR
    /*
     * Interface address is needed by some IPv4 APIs.
     */
#ifdef HAVE_GETIFADDRS
    {
        /* use getifaddrs() to get interface address */
        struct ifaddrs *ifp, *ifpt;
        int ok = 0;

        ifp = NULL;
        if (getifaddrs(&ifp) < 0) {
            snprintf(errbuf, errlen,
                     "interface address lookup error: %s", strerror(errno));
            return;
        }
        for (ifpt = ifp; ifpt; ifpt = ifpt->ifa_next) {
            /* here's one address, is it the one we want? */
            if (ifpt->ifa_name == NULL) continue;           /* no name */
            if (strcmp(ifpt->ifa_name, intf->nam)) continue;/* other name */
            if (ifpt->ifa_addr == NULL) continue;           /* no address */
            if (ifpt->ifa_addr->sa_family != AF_INET)
                continue;                                   /* not IPv4 */

            /* got it! */
            intf->adr = ((struct sockaddr_in *)ifpt->ifa_addr)->sin_addr;
            ok = 1;
            break;
        }
        if (ifp) {
            freeifaddrs(ifp);
        }
        if (!ok) {
            snprintf(errbuf, errlen, "IPv4 address not found for '%s'",
                     intf->nam);
            return;
        }
    }
#else /* HAVE_GETIFADDRS */
#error "no way coded to get interface address on this platform"
    /* there may be a way, but the code for it hasn't been written */
#endif /* HAVE_GETIFADDRS */
#endif /* WANT_IFADDR */
}

