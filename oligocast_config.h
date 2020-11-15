/*
 * oligocast_config.h
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
 * oligocast_config.h
 *
 * Build-time settings for "oligocast".  You might edit these to adapt
 * oligocast to your needs, mainly to the platform you're going to
 * compile and run it on.
 *
 * The first bunch of #defines relate to operating system compatibility
 * and functionality.  The second bunch of defines related to defaults.
 */

/*
 * #defines for operating system compatibility and functionality:
 *      DO_SOURCES
 *          Enable source specific multicast functionality.
 *          Recommendation: #define this if you can.  Hopefully you can.
 *      HAVE_SETSOURCEFILTER
 *          Use RFC 3678's fanciest interface setsourcefilter().
 *          Recommendation: #define this on most platforms.  It seems to
 *          be a well established standard.
 *      HAVE_IPV6_JOIN_GROUP
 *          Use RFC 3493's IPV6_JOIN_GROUP socket option to join IPv6
 *          multicast groups.
 *          Recommendation: #define this on most platforms.  It's a standard.
 *          But Linux, if it has it, doesn't document it.
 *      HAVE_IP_ADD_MEMBERSHIP_IP_MREQN
 *          The IP_ADD_MEMBERSHIP socket option can take 'struct ip_mreqn'
 *          which is nice.
 *          Recommendation: #define this on Linux 2.2 and later.
 *      NEED_MEMBERSHIP_FIRST
 *          Indicates that whatever API is used to set up source filtering
 *          (e.g. setsourcefilter()) requires that the multicast group
 *          be joined separately.
 *          Recommended: Needed on Linux, possibly other systems.
 *      HAVE_MULTICAST_ALL
 *          Use IP_MULTICAST_ALL socket option.
 *          Recommendation: #define this on Linux 2.6.31 and later.
 *      HAVE_V6_MULTICAST_ALL
 *          Use IPV6_MULTICAST_ALL socket option.
 *          Recommendation: #define this on Linux 4.20 and later, unless
 *          it causes a compilation error; some Linux systems have older
 *          headers.
 *      HAVE_MULTICAST_IF_IP_MREQN
 *          The IP_MULTICAST_IF socket option can take 'struct ip_mreqn'
 *          which is nice.
 *          Recommendation: #define this on Linux 3.5 and later.
 *      HAVE_GETIFADDRS
 *          Use getifaddrs() to get information about network devices.
 *          Recommendation: #define this on BSD systems, and on Linux since
 *          glibc 2.3.
 *      HAVE_SA_LEN
 *          'struct sockaddr' has a member 'sa_len' and it needs to be filled
 *          in with the right length.
 *          Recommendation: #define this on BSD systems; not on Linux
 */

/*
 * There's some logic below to try to autodetect what platform you're
 * compiling for, and determine what it has, but it's very basic.
 * If you have trouble, edit the #defines below to suit the reality of
 * what you've got.
 */

#define DO_SOURCES
#define HAVE_SETSOURCEFILTER
#define NEED_MEMBERSHIP_FIRST
#if defined(__linux__)
#define HAVE_IPV6_JOIN_GROUP
#define HAVE_MULTICAST_ALL
/* #define HAVE_V6_MULTICAST_ALL */
#define HAVE_MULTICAST_IF_IP_MREQN
#define HAVE_IP_ADD_MEMBERSHIP_IP_MREQN
#else /* __linux__ */
#define HAVE_IPV6_JOIN_GROUP
#define HAVE_SA_LEN
#endif /* !__linux__ */
#define HAVE_GETIFADDRS

/*
 * #defines related to defaults:
 *      DEF_TTL
 *          Default hop limit / time to live value to use.  Overridden
 *          at runtime with the "-T" command line option.
 *          overridden at runtime with the "-T" command line option.
 *      DEF_UDP_PORT
 *          UDP port number for sending and receiving packets.  Overridden
 *          at runtime with the "-p" command line option.
 *      DEF_IPV4_GROUP
 *      DEF_IPV6_GROUP
 *          Multicast group address, for use with IPv4 and IPv6 respectively.
 *          Overridden at runtime with the "-g" command line option.
 *          Doing so is recommended: there are no known "particularly right"
 *          values for defaults.
 */

#define DEF_TTL 4 /* reasonable compromise */
/* #define DEF_TTL 1 */ /* more conservative; also "mtools" uses this */
/* #define DEF_TTL -1 */ /* -1 uses operating system default */
#define DEF_UDP_PORT 4444 /* historical: "mtools" uses this */
/* #define DEF_UDP_PORT 44444 */ /* not in the reserved range */
#define DEF_IPV4_GROUP "224.1.1.1" /* historical: "mtools" uses this */
/* #define DEF_IPV4_GROUP "232.1.1.1" */ /* early "oligocast" used this */
/* #define DEF_IPV4_GROUP "239.1.1.1" */ /* is in admin-scoped block */
#define DEF_IPV6_GROUP "ff15::abcd" /* site-scoped any-source multicast */
/* #define DEF_IPV6_GROUP "ff35::abcd" */ /* early "oligocast" used this */

