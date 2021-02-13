/*
 * oligocast.c
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
 * "oligocast" is a test program for IP multicast (IPv4 and IPv6).  It's
 * meant to add a few things lacking from the "mtools" package:
 *      + Can do IPv4 or IPv6 in the same program
 *      + Can do source-specific multicast; and change source filters on the fly
 *      + Can transmit and receive in the same program (different processes)
 *      + On Linux, aware of net namespaces
 *      + Receiver has a "quiet" output mode in which, instead of
 *      reporting every packet, only reports when it starts/stops
 *      receiving packets.
 *      + Can "label" its output with information chosen by the user
 *
 * Example usage:
 *      oligocast -t -g 232.1.2.3 -i eth1 -T -
 *          send IPv4 packets on eth1, to group 225.1.2.3, with system
 *          default TTL
 *      oligocast -r -g ff35::bbb -i eth2
 *          receive IPv6 packets on eth2, from group ff35::bbb
 *
 * Normally direction is specified by the -t / -r options.  But if
 * you make links to the "oligocast" executable, named things like
 * "oligosend", "oligoreceive", "oligotx", "oligorx", it will take
 * the direction from the command name instead.
 */

/*
 * This implementation relies on the interfaces defined in RFC 3493 and
 * RFC 3678.  Current systems should have them; others may have trouble.
 */

#include "oligocast_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <libgen.h>
#include "oligocast.h"

/** ** type definitions and prototypes ** **/

enum reported_events {
    /* things which happen & we can report through emit() */
    reported_event_tx,                  /* packet sent */
    reported_event_rx,                  /* packet received */
    reported_event_up,                  /* packet received when down */
    reported_event_dn,                  /* time out, no packet received */
    reported_event_cmd,                 /* command received and handled */
    reported_event_note,                /* informational note */
};

struct config {
    /* Configuration settings, combined in one more or less handy package. */

    int                     cfg_dir;        /* direction: TX >0, RX <0 */
    int                     cfg_af;         /* address family (inferred) */
    struct sockaddr_storage cfg_grp;        /* multicast group */
    socklen_t               cfg_grplen;     /* size of cfg_grp */
    int                     cfg_port;       /* UDP port number */
    struct oligocast_if     cfg_intf;       /* network interface */
    int                     cfg_ttl;        /* time to live / hop limit */
#ifdef DO_SOURCES
    /* Source filtering settings:
     *      Apparent current working setting:
     *          cfg_osfmode -- mode: MCAST_{IN,EX}CLUDE
     *          cfg_osources -- array of source addresses
     *          cfg_onsources -- number of entries in cfg_osources
     *      Desired new setting:
     *          cfg_sfmode -- mode: MCAST_{IN,EX}CLUDE
     *          cfg_sources -- array of source addresses
     *          cfg_nsources -- number of entries in cfg_sources
     */
    uint32_t                cfg_sfmode;
    struct sockaddr_storage *cfg_sources;
    int                     cfg_nsources;
    uint32_t                cfg_osfmode;
    struct sockaddr_storage *cfg_osources;
    int                     cfg_onsources;
#endif /* DO_SOURCES */
    int                     cfg_verbose;    /* report each packet */
    char *                  cfg_label;      /* output label */
    char *                  cfg_label_csv;  /* cfg_label, CSV-escaped */
    int                     cfg_csv;        /* CSV-formatted output */
    float                   cfg_period;     /* seconds between packets */
    long                    cfg_period_us;  /* cfg_period as microseconds */
    float                   cfg_multiplier; /* this times cfg_period = timeout*/
    long                    cfg_timeout_us; /* microseconds timeout */
    uint8_t *               cfg_data;       /* data to send */
    size_t                  cfg_data_len;   /* number of bytes in cfg_data */
    int                     cfg_join;       /* join even when transmitting */
    int                     cfg_command_in; /* allow commands on stdin */
    char                    cfg_command_buf[4096]; /* partial commands read */
    int                     cfg_command_got;/* bytes in cfg_command_buf[] */
    int                     cfg_command_ignore; /* ignore command */
    struct oligocast_sml_state cfg_sml_state; /* for setup_mcast_listen() */
};

enum command_action {
    /*
     * An action, that might be triggered by a command, that isn't "just" a
     * configuration change.
     */
    command_action_none,                /* no action */
    command_action_wait,                /* waiting for more input */
    command_action_error,               /* treat as error */
#ifdef DO_SOURCES
    command_action_source,              /* source filter mode/list change */
#endif /* DO_SOURCES */
    command_action_exit_program,        /* terminate the program */
    command_action_time_change,         /* period/multiplier/timeout changed */
};

static void usage(void);
static enum command_action option(struct config *cfg,
                                  int pc, int oc, char *arg);
static enum command_action source_option(struct config *cfg,
                                         int pc, int oc, char *arg);
static enum command_action data_option(struct config *cfg, char *arg);
static enum command_action format_option(struct config *cfg,
                                         int pc, int oc, char *arg);
static enum command_action command(struct config *cfg);
static void progname_to_progdir(void);
static void errout(char *fmt, ...);
static void errthrottle(void);
static void emit(struct config *cfg, enum reported_events evt, char *extra);
static int auto_pton(char *s, struct sockaddr_storage *ss,
                     socklen_t *sslen, struct config *cfg);
static char *auto_ntop(void *src, char *dst, socklen_t size);
#ifdef DO_SOURCES
static void sort_addrs(struct sockaddr_storage *addrs, int naddrs);
static int sort_addrs_compare(const void *x, const void *y);
static void add_addrs(struct sockaddr_storage **res_addrs, int *res_naddrs,
                      struct sockaddr_storage *lft_addrs, int lft_naddrs,
                      struct sockaddr_storage *rgt_addrs, int rgt_naddrs);
static void sub_addrs(struct sockaddr_storage **res_addrs, int *res_naddrs,
                      struct sockaddr_storage *inc_addrs, int inc_naddrs,
                      struct sockaddr_storage *exc_addrs, int exc_naddrs);
#endif /* DO_SOURCES */
static int timestamp_log(struct timeval *tv, char *buf, size_t len, void *arg);
static int timestamp_raw(struct timeval *tv, char *buf, size_t len, void *arg);
static int timestamp_num(struct timeval *tv, char *buf, size_t len, void *arg);
static int timestamp_none(struct timeval *tv, char *buf, size_t len, void *arg);
static char *csv_escape(char *s);
static char *make_default_label(struct config *cfg);
static void group_check(struct config *cfg, int first_time);

/** ** configuration ** **/

static char *progname = "oligocast";
static int progdir = 0; /* progname implies TX (>0) or RX (<0) or neutral */
static int (*timestamp_formatter)(struct timeval *, char *, size_t, void *) =
    timestamp_log;
static void *timestamp_formatter_arg = NULL;

/*
 * usage()
 * Display a help message.
 */
static void usage(void)
{
    fprintf(stderr,
            "USAGE: %s options...\n"
            "OPTIONS:\n",
            progname);
    if (progdir == 0) {
        fprintf(stderr,
            "    -t -- transmit (send)\n"
            "    -r -- receive\n");
    }
    fprintf(stderr,
            "    -g grp -- multicast group address to listen to\n"
            "    -p port -- UDP port number to use\n"
            "    -i iface -- name of network interface to use\n");
    if (progdir >= 0) {
        fprintf(stderr,
            "    -T ttl -- time to live / hop limit value to use;\n"
            "              \"-\" for system default; default %d\n",
            (int)DEF_TTL);
    }
#ifdef DO_SOURCES
    fprintf(stderr,
            "    -E addr(s) -- exclude addrs; see SOURCES\n"
            "    -I addr(s) -- include addrs; see SOURCES\n");
#endif /* DO_SOURCES */
    fprintf(stderr,
            "    -v -- verbose mode: report each packet send/received\n"
            "    -l label -- include label string in output\n"
            "    -f formopt -- formatting option for output:\n"
            "        -f csv -- CSV format the output\n"
            "        -f nocsv -- don't CSV format the output (default)\n"
            "        -f logtime -- timestamps like: Sep 12 00:01:17.123\n"
            "                      (the default)\n"
            "        -f rawtime -- timestamps like: 1599943404.456\n"
            "        -f numtime -- timestamps like: 2020-09-12-13:46:43.789\n"
            "        -f notime -- no timestamps\n"
            "    -P sec -- period between packets in seconds; default 1.0\n");
    if (progdir <= 0) {
        fprintf(stderr,
            "    -m mult -- multiply packet period to get timeout\n");
    }
    fprintf(stderr,
            "    -d data -- message data to send:\n"
            "        hex:ABCDEF -- some bytes in hexadecimal\n"
            "        text:abcdef -- some literal text\n");
    if (progdir >= 0) {
        fprintf(stderr,
            "    -j -- join the multicast group even when transmitting\n");
    }
    fprintf(stderr,
            "    -k -- enable reading comments from stdin; see COMMANDS\n");

    fprintf(stderr,
            "\n"
            "COMMANDS:\n"
            "    With the -k option, this program can take commands on stdin.\n"
            "    Each command is a line by itself, beginning with a one or\n"
            "    two character command code, followed by a space and\n"
            "    an argument if it takes one.  The command codes are\n"
            "    as follows:\n"
            "    #\n"
            "        comment; ignore the whole line\n"
#ifdef DO_SOURCES
            "    -E, -I, -v, -l, -f, -P%s, -d\n"
#else
            "    -v, -l, -f, -P%s, -d\n"
#endif /* DO_SOURCES */
            "        same as the command line options\n"
            "    +v, +k\n"
            "        opposites of the command line options\n"
            "    ?E, ?I\n"
            "        state queries related to the command line options\n"
            "    ..\n"
            "        command does nothing; but is echoed with timestamp\n"
            "    .x\n"
            "        terminate the program\n",
            (progdir <= 0) ? ", -m" : "");

#ifdef DO_SOURCES
    fprintf(stderr,
            "\n"
            "SOURCES:\n"
            "    The -E and -I options (and the -E and -I commands)\n"
            "    are used to specify source addresses and modes.\n"
            "    There are two modes:\n"
            "        -E -- \"Exclude\" mode allows all sources except those\n"
            "              specified.\n"
            "        -I -- \"Include\" mode allows only those specified.\n"
            "    The values for these options are comma delimited lists\n"
            "    of source IP addresses.  Example: -E 1.2.3.4,2.3.4.5\n"
            "    You can also use \"-\" for the list to leave it empty.\n"
            "\n"
            "    On the command line you may only specify one of the two\n"
            "    options.  In command input you may repeat them, either\n"
            "    replacing or augmenting the existing source lists as\n"
            "    follows:\n"
            "        Any existing list is replaced (clobbered) by a new\n"
            "        \"-E\" or \"-I\" command by default.\n"
            "        When an \"-E\" follows other \"-E\" (or an \"-I\"\n"
            "        follows other \"-I\") the list may be prefixed with\n"
            "        \"+\" or \"-\" to add to (or subtract from) the existing\n"
            "        list instead of replacing it.\n");
#endif /* DO_SOURCES */
}

/*
 * option()
 *
 * Handle a command line option, or a command from stdin.
 *
 * Parameters:
 *      cfg - configuration structure, where things get stored
 *      pc - prefix character:
 *          '\0' - when called from command line
 *          '-' - for a '-' command on stdin
 *          '+' - for a '+' command on stdin
 *          '.' - for a '.' command on stdin
 *      oc - option character: example 'I' for the '-I' option
 *      arg - argument for this option/command if any
 *
 * Return value:
 *      What (if any) additional action needs to be taken in response
 *      to the command.
 */
static enum command_action option(struct config *cfg, int pc, int oc, char *arg)
{
    float f;
    char errbuf[256];

    switch (oc) {
    case 't': /* -t option on command line: transmit (send) */
        if (progdir != 0) {
            errout("-t/-r may not be used with command name '%s',"
                   " which determines direction", progname);
            return(command_action_error);
        }
        if (cfg->cfg_dir != 0 || pc != 0) {
            errout("-t/-r may not be used more than once");
            return(command_action_error);
        }
        cfg->cfg_dir = 1;
        break;

    case 'r': /* -r option on command line: receive */
        if (progdir != 0) {
            errout("-t/-r may not be used with command name '%s',"
                " which determines direction", progname);
            return(command_action_error);
        }
        if (cfg->cfg_dir != 0 || pc != 0) {
            errout("-t/-r may not be used more than once");
            return(command_action_error);
        }
        cfg->cfg_dir = -1;
        break;

    case 'g': /* -g option on command line: specify the multicast group */
        if (pc != 0) {
            errout("-g may only appear on the command line");
            return(command_action_error);
        }
        if (cfg->cfg_grp.ss_family != AF_UNSPEC) {
            errout("-g may not be used more than once");
            return(command_action_error);
        }
        cfg->cfg_grplen = sizeof(cfg->cfg_grp);
        if (!auto_pton(arg, &cfg->cfg_grp, &cfg->cfg_grplen, cfg)) {
            return(command_action_error);
        }
        break;

    case 'p': /* -p option on command line: specify the UDP port number */
        if (pc != 0) {
            errout("-p may only appear on the command line");
            return(command_action_error);
        }
        if (cfg->cfg_port != 0) {
            errout("-p may not be used more than once");
            return(command_action_error);
        }
        cfg->cfg_port = atoi(arg);
        if (cfg->cfg_port < 1 || cfg->cfg_port > 65535) {
            errout("-p port must be in range 1-65535");
            return(command_action_error);
        }
        break;

    case 'i': /* -i option on command line: specify the network interface */
        if (pc == '?') {
            /* for testing: display interface information */
            snprintf(errbuf, sizeof(errbuf),
                     "interface info: name '%s' index %u addr 0x%x",
                     cfg->cfg_intf.nam, (unsigned)cfg->cfg_intf.idx,
                     (unsigned)cfg->cfg_intf.adr.s_addr);
            emit(cfg, reported_event_note, errbuf);
            return(command_action_none);
        }
        if (pc != 0) {
            errout("-i may only appear on the command line");
            return(command_action_error);
        }
        if (cfg->cfg_intf.nam[0] != '\0') {
            errout("-i may not be used more than once");
            return(command_action_error);
        }
        identify_interface(arg, &cfg->cfg_intf, errbuf, sizeof(errbuf));
        if (errbuf[0]) {
            errout("%s", errbuf);
            return(command_action_error);
        }
        break;

    case 'T': /* -T option on command line: specify TTL / hop limit value */
        if (pc != '\0') {
            errout("-T may only appear on the command line");
            return(command_action_error);
        }
        if (!strcmp(arg, "-")) {
            cfg->cfg_ttl = -1;
        } else {
            cfg->cfg_ttl = atoi(arg);
            if (cfg->cfg_ttl < 0 || cfg->cfg_ttl > 255) {
                errout("TTL/hop limit value '%s' outside range 0-255", arg);
                return(command_action_error);
            }
        }
        break;

    case 'E': /* -E option or command: exclude sources */
        return(source_option(cfg, pc, oc, arg));

    case 'I': /* -I option or command: include sources */
        return(source_option(cfg, pc, oc, arg));

    case 'v':
        if (pc == '\0' || pc == '-') {
            cfg->cfg_verbose++;
        } else if (pc == '+') {
            cfg->cfg_verbose = 0;
        } else {
            errout("%cv is not a valid command", (int)pc);
        }
        break;

    case 'l': /* -l option or command: set output label */
        if (pc != '-' && pc != '\0') {
            errout("%c%c is not a valid command", pc, oc);
            return(command_action_error);
        }
        if (cfg->cfg_label) {
            free(cfg->cfg_label);
        }
        if (cfg->cfg_label_csv) {
            free(cfg->cfg_label_csv);
        }
        cfg->cfg_label = strdup(arg);
        cfg->cfg_label_csv = csv_escape(arg);
        break;

    case 'f': /* -f option or command: formatting settings */
        return(format_option(cfg, pc, oc, arg));

    case 'P': /* -P option or command: set period */
        if (pc != '-' && pc != '\0') {
            errout("%c%c is not a valid command", pc, oc);
            return(command_action_error);
        }
        f = atof(arg);
        if (!(f >= 0.001 && f <= 60.0)) {
            errout("-P period must be in range 0.001-60 seconds");
            return(command_action_error);
        }
        cfg->cfg_period = f;
        return(command_action_time_change);

    case 'm': /* -m option or command: set multiplier */
        if (pc != '-' && pc != '\0') {
            errout("%c%c is not a valid command", pc, oc);
            return(command_action_error);
        }
        f = atof(arg);
        if (!(f >= 1.1 && f <= 10.0)) {
            errout("-m multiplier must be in range 1.1-10");
            return(command_action_error);
        }
        cfg->cfg_multiplier = f;
        return(command_action_time_change);

    case 'd': /* -d option or command: data to send/expect in messages */
        if (pc != '-' && pc != '\0') {
            errout("%c%c is not a valid command", pc, oc);
            return(command_action_error);
        }
        return(data_option(cfg, arg));

    case 'j': /* -j join multicast group even when sending */
        if (pc != '\0') {
            errout("-j only allowed on command line");
            return(command_action_error);
        }
        cfg->cfg_join = 1;
        break;

    case 'k': /* -k listen for commands on stdin */
        if (pc == '+') {
            cfg->cfg_command_in = 0;
            cfg->cfg_command_got = 0;
            cfg->cfg_command_ignore = 0;
        } else if (pc == '-' || pc == '\0') {
            cfg->cfg_command_in = 1;
        } else {
            errout("%ck is not a valid command", (int)pc);
            return(command_action_error);
        }
        break;

    case 'x': /* .x exit the program */
        if (pc == '.') {
            return(command_action_exit_program);
        } else {
            errout("%c%c is not a valid command", pc, oc);
            return(command_action_error);
        }
        break;

    case '.': /* .. no operation (command is echoed) */
        return(command_action_none);

    default:
        if (pc == '\0') {
            errout("-%c is not a valid option", oc);
        } else {
            errout("%c%c is not a valid command", pc, oc);
        }
        return(command_action_error);
    }

    return(command_action_none);
}

/*
 * source_option()
 *
 * Handle a -I or -E command line option, or the same command from stdin.
 * These manipulate lists of source addresses for filtering.
 *
 * Parameters:
 *      cfg - configuration structure, where things get stored
 *      pc - prefix character:
 *          '\0' when called from command line
 *          '-' when called from stdin
 *          other when called wrong
 *      oc - option character: 'I' (include) or 'E' (exclude)
 *      arg - argument string; representing one or more addresses
 *          separated by ','; possibly prefixed by '-' or '+' for
 *          deltas; or replaced by '-' for empty list
 *
 * Return value:
 *      What (if any) additional action needs to be taken in response
 *      to the command.
 */
static enum command_action source_option(struct config *cfg,
                                         int pc, int oc, char *arg)
{
#ifdef DO_SOURCES
    uint32_t newmode;
    int delta = 0, pos = 0, len, end, i;
    struct sockaddr_storage *sources; /* source list in the option */
    int nsources, asources;
    struct sockaddr_storage *csources; /* combined sources */
    int cnsources;
    socklen_t srclen;

    /* special case for query option, "?E" / "?I" */
    if (pc == '?') {
        char *res;
        int resa, resl;

        /* allocate space to represent the source list */
        resa = 32 + 48 * cfg->cfg_nsources;
        res = calloc(resa, 1);

        /* represent the source list */
        resl = snprintf(res, resa, "source setting: %s%s",
                        (cfg->cfg_sfmode == MCAST_INCLUDE) ? "-I" : "-E",
                        cfg->cfg_nsources ? "" : "-");
        for (i = 0; i < cfg->cfg_nsources; ++i) {
            if (resl + 2 < resa) {
                if (i) {
                    res[resl++] = ',';
                    res[resl] = '\0';
                }
                resl += strlen(auto_ntop(&cfg->cfg_sources[i],
                                         res + resl, resa - resl));
            } else {
                res[0] = '?';
                res[1] = '\0';
                resl = 1;
                break;
            }
        }

        /* emit a message about it */
        emit(cfg, reported_event_note, res);
        free(res);
        return(command_action_none);
    }

    /* sanity checks */
    if (pc != '\0' && pc != '-') {
        errout("%c%c is not a valid command", pc, oc);
        return(command_action_error);
    }

    /* include or exclude mode? */
    switch (oc) {
    case 'E':           newmode = MCAST_EXCLUDE; break;
    case 'I': default:  newmode = MCAST_INCLUDE; break;
    }

    /* '+' or '-' prefix for deltas? */
    if ((arg[pos] == '+' || arg[pos] == '-') && arg[pos + 1] != '\0') {
        delta = arg[pos];
        ++pos;
    }
    if (delta != '\0' && pc == '\0') {
        errout("-%c doesn't take +/- deltas on command line", oc);
        return(command_action_error);
    }
    if (delta != '\0' && newmode != cfg->cfg_sfmode) {
        errout("-%c doesn't take +/- deltas when changing mode", oc);
        return(command_action_error);
    }

    /* how many sources in the source list? */
    if (arg[0] == '-' && arg[1] == '\0') {
        /* empty list */
        asources = pos = 1;
    } else {
        asources = 1;
        for (i = pos; arg[i]; ++i) {
            if (arg[i] == ',') {
                ++asources;
            }
        }
    }

    /* parse the source list */
    sources = calloc(asources, sizeof(sources[0]));
    nsources = 0;
    while (arg[pos]) {
        if (nsources >= asources) {
            /* shouldn't happen */
            errout("internal error in source count in parsing -%c", oc);
            free(sources);
            return(command_action_error);
        }
        len = strcspn(arg + pos, ",");
        end = (arg[pos + len] == '\0');
        arg[pos + len] = '\0'; /* temporary change */
        srclen = sizeof(sources[nsources]);
        if (!auto_pton(arg + pos, &sources[nsources], &srclen, cfg)) {
            free(sources);
            return(command_action_error);
        }
        ++nsources;
        pos += len;
        if (!end) {
            arg[pos] = ','; /* undo temporary change */
            ++pos;
        }
    }

    /* figure out the combined list of sources */
    sort_addrs(sources, nsources);
    csources = NULL;
    cnsources = 0;
    switch (delta) {
    case '\0':
        /* no delta, just replace the list */
        csources = sources;
        cnsources = nsources;
        break;
    case '+':
        /* add */
        add_addrs(&csources, &cnsources,
                  cfg->cfg_sources, cfg->cfg_nsources,
                  sources, nsources);
        free(sources);
        break;
    case '-':
        /* subtract */
        sub_addrs(&csources, &cnsources,
                  cfg->cfg_sources, cfg->cfg_nsources,
                  sources, nsources);
        free(sources);
        break;
    default:
        /* shouldn't happen */
        errout("internal error in delta code in parsing -%c", oc);
        free(sources);
        return(command_action_error);
    }

    /* store the new sources to apply later */
    if (cfg->cfg_sources) {
        free(cfg->cfg_sources);
        cfg->cfg_sources = NULL;
        cfg->cfg_nsources = 0;
    }
    cfg->cfg_sfmode = newmode;
    cfg->cfg_nsources = cnsources;
    cfg->cfg_sources = csources;

    return(command_action_source);
#else /* DO_SOURCES */
    errout("-%c not supported in this build", oc);
    return(command_action_error);
#endif /* DO_SOURCES */
}

/*
 * data_option()
 *
 * Handle a -d command line option, or the same command from stdin.
 * This sets up data to include/expect in packets.
 *
 * Parameters:
 *      cfg - configuration structure, where things get stored
 *      arg - argument string
 *
 * Return value:
 *      What (if any) additional action needs to be taken in response
 *      to the command.
 */
static enum command_action data_option(struct config *cfg, char *arg)
{
    uint8_t *data;
    int len, i, o, ib;
    char *s;

    if (!strncmp(arg, "hex:", 4)) {
        /* hexadecimal; example "hex:68656c6c6f" */
        len = (strlen(arg) - 4) >> 1;
        if (strlen(arg) & 1) {
            errout("Odd number of digits in -d option");
            return(command_action_error);
        }
        data = calloc(len, 1);
        ib = 4; /* input base */
        for (i = ib; arg[i]; ++i) {
            o = (i - ib) >> 1; /* output offset */
            data[o] <<= 4;
            if (arg[i] >= '0' && arg[i] <= '9') {
                data[o] |= arg[i] - '0';
            } else if (arg[i] >= 'A' && arg[i] <= 'F') {
                data[o] |= arg[i] - 'A' + 10;
            } else if (arg[i] >= 'a' && arg[i] <= 'f') {
                data[o] |= arg[i] - 'a' + 10;
            } else {
                errout("Non hex digit character (%d) in -d option",
                       (int)arg[i]);
                free(data);
                return(command_action_error);
            }
        }
    } else if (!strncmp(arg, "text:", 5)) {
        /* plain text: just copy it */
        s = strdup(arg + 5);
        len = strlen(s);
        data = (void *)s;
    } else {
        errout("Unrecognized format in -d option");
        return(command_action_error);
    }

    /* store the result */
    if (cfg->cfg_data) {
        free(cfg->cfg_data);
    }
    cfg->cfg_data = data;
    cfg->cfg_data_len = len;
    return(command_action_none);
}

/*
 * format_option()
 *
 * Handle a -f command line option, or the same command from stdin.
 * This has keyword suboptions related to output formatting.
 *
 * Parameters:
 *      cfg - configuration structure, where things get stored
 *      pc - prefix character:
 *          '\0' when called from command line
 *          '-' when called from stdin
 *          other when called wrong
 *      oc - option character: 'I' (include) or 'E' (exclude)
 *      arg - argument string; representing one or more addresses
 *          separated by ','; possibly prefixed by '-' or '+' for
 *          deltas; or replaced by '-' for empty list
 *
 * Return value:
 *      What (if any) additional action needs to be taken in response
 *      to the command.
 */
static enum command_action format_option(struct config *cfg,
                                         int pc, int oc, char *arg)
{
    if (pc != '\0' && pc != '-') {
        errout("%cf is not a valid command", (int)pc);
        return(command_action_error);
    }
    if (!strcasecmp(arg, "csv")) {
        /* -f csv -- enable CSV output */
        cfg->cfg_csv = 1;
        return(command_action_none);
    } else if (!strcasecmp(arg, "nocsv")) {
        /* -f nocsv -- disable CSV output */
        cfg->cfg_csv = 0;
        return(command_action_none);
    } else if (!strcasecmp(arg, "logtime")) {
        /* -f logtime -- "log" timestamp format */
        timestamp_formatter = &timestamp_log;
        timestamp_formatter_arg = NULL;
        return(command_action_none);
    } else if (!strcasecmp(arg, "rawtime")) {
        /* -f rawtime -- "raw" timestamp format */
        timestamp_formatter = &timestamp_raw;
        timestamp_formatter_arg = NULL;
        return(command_action_none);
    } else if (!strcasecmp(arg, "numtime")) {
        /* -f numtime -- "num" timestamp format */
        timestamp_formatter = &timestamp_num;
        timestamp_formatter_arg = NULL;
        return(command_action_none);
    } else if (!strcasecmp(arg, "notime")) {
        /* -f notime -- no timestamps */
        timestamp_formatter = &timestamp_none;
        timestamp_formatter_arg = NULL;
        return(command_action_none);
    } else {
        errout("-f %s is not a valid formatting option", arg);
        return(command_action_error);
    }
}

/*
 * command()
 *
 * Parse and execute a command.
 *
 * Parameters:
 *      cfg - configuration structure, where things get stored
 *
 * Return value:
 *      What (if any) additional action needs to be taken in response
 *      to the command.
 */
static enum command_action command(struct config *cfg)
{
    int l, pos;
    char *cmd;
    enum command_action ca;

    /* is there a complete line in the buffer? */
    for (l = 0; l < cfg->cfg_command_got; ++l) {
        if (cfg->cfg_command_buf[l] == '\n') {
            break;
        }
    }
    if (l >= cfg->cfg_command_got) {
        /* nope */
        if (cfg->cfg_command_got >= sizeof(cfg->cfg_command_buf)) {
            if (!cfg->cfg_command_ignore) {
                errout("ultra-long command line ignored");
                cfg->cfg_command_ignore = 1;
            }
            cfg->cfg_command_got = 0;
            return(command_action_error);
        } else {
            return(command_action_wait);
        }
    }

    /* extract command into a new buffer, without any initial whitespace */
    for (pos = 0; pos < l && isspace(cfg->cfg_command_buf[pos]); ++pos)
        ;
    cmd = malloc(l - pos + 1);
    memcpy(cmd, &cfg->cfg_command_buf[pos], l - pos);
    cmd[l - pos] = '\0';
    if (l < cfg->cfg_command_got) {
        ++l; /* clobber the newline at the end */
    }
    memmove(&cfg->cfg_command_buf[0], &cfg->cfg_command_buf[l],
            cfg->cfg_command_got - l);
    cfg->cfg_command_got -= l;

    /* ignore the command if desired */
    if (cfg->cfg_command_ignore) {
        cfg->cfg_command_ignore = 0;
        free(cmd);
        return(command_action_none);
    }

    /* remove any final whitespace */
    l = strlen(cmd);
    while (l > 0 && isspace(cmd[l - 1])) {
        cmd[--l] = '\0';
    }

    /* ignore empty lines and '#' comments */
    if (l <= 0 || cmd[0] == '#') {
        free(cmd);
        return(command_action_none);
    }
    emit(cfg, reported_event_cmd, cmd);

    /*
     * command format:
     *      prefix char: '-', '+', '.'
     *      operation char: anything
     *      zero or more spaces
     *      argument
     */
    if (l < 2) {
        errout("Invalid command '%s'", cmd);
        free(cmd);
        return(command_action_error);
    }
    pos = 2;
    while (pos < l && isspace(cmd[pos]))
        ++pos;
    ca = option(cfg, cmd[0], cmd[1], cmd + pos);

    free(cmd);

    return(ca);
}

/*
 * progname_to_progdir()
 *
 * See if the program name (in 'progname') indicates the direction
 * (send/receive) this is to do.  Sets 'progdir' to the result. 
 */
static void progname_to_progdir(void)
{
    struct {
        char *s;
        int d;
    } endings[] = {
        { "send",       1 },
        { "receive",    -1 },
        { "recv",       -1 },
        { "snd",        1 },
        { "rcv",        -1 },
        { "tx",         1 },
        { "rx",         -1 },
        { NULL, 0 }
    };
    int pnl, ei, el;
    char *cp;

    /* any '.' extension? */
    cp = strrchr(progname, '.');
    if (cp != NULL) {
        pnl = cp - progname;
    } else {
        pnl = strlen(progname);
    }

    /* see about command names ending with interesting hints */
    for (ei = 0; endings[ei].s; ++ei) {
        el = strlen(endings[ei].s);
        if (pnl >= el &&
            !strncasecmp(progname + pnl - el, endings[ei].s, el)) {
            /* matched! */
            progdir = endings[ei].d;
            return;
        }
    }
}

/*
 * make_default_label()
 * Since the user didn't specify a label, make one based on their
 * configuration.  Returns a newly allocated string; caller should
 * free it when no longer wanted.
 */
static char *make_default_label(struct config *cfg)
{
    char ga[128], buf[256];
    auto_ntop(&cfg->cfg_grp, ga, sizeof(ga));
    snprintf(buf, sizeof(buf), "%s%%%s", ga, cfg->cfg_intf.nam);
    return(strdup(buf));
}

/*
 * group_check()
 *
 * Check the config, for whether the multicast group address is actually
 * a multicast address, and if needed, a source-specific multicast address.
 * Gives a warning if not.  It's only a warning, and doesn't catch all
 * restrictions and requirements for multicast addressing.  This program
 * isn't the standards police; if you do something you shouldn't be doing,
 * that's your problem.
 *
 * Standards:
 *      RFC 4607 for source specific multicast addresses
 *      RFC 1112 for IPv4 multicast addresses
 *      RFC 4291 for IPv6 multicast addresses
 *
 * Parameters:
 *      cfg -- configuration including group and sources
 *      first_time -- Nonzero to indicate this is being run the first
 *          time; zero if not.  The first time it runs more checks are done.
 */
static void group_check(struct config *cfg, int first_time)
{
    char gbuf[128];
#ifdef DO_SOURCES
    int ssm_group, ssm_filter;
#endif /* DO_SOURCES */

    if (first_time) {
        if (cfg->cfg_grp.ss_family == AF_INET6) {
            /* RFC 4291 says IPv6 multicast groups are in ff00::/8 */
            struct sockaddr_in6 *a = (void *)&cfg->cfg_grp;
            uint8_t *b = (void *)&(a->sin6_addr);
            if (b[0] != 0xff) {
                errout("warning: %s is not a multicast group",
                       auto_ntop(&cfg->cfg_grp, gbuf, sizeof(gbuf)));
                return;
            }
        } else {
            /* RFC 1112 says IPv4 multicast groups are in 224.0.0.0/4 */
            struct sockaddr_in *a = (void *)&cfg->cfg_grp;
            uint8_t *b = (void *)&(a->sin_addr);
            if ((b[0] & 240) != 224) {
                errout("warning: %s is not a multicast group",
                       auto_ntop(&cfg->cfg_grp, gbuf, sizeof(gbuf)));
                return;
            }
        }
    }

#ifdef DO_SOURCES
    if (cfg->cfg_join || cfg->cfg_dir <= 0) {
        /* is this a source-specific multicast group? */
        ssm_group = 0;
        if (cfg->cfg_grp.ss_family == AF_INET6) {
            /* RFC 4607 says IPv6 SSM groups are in FF3x::/32 */
            struct sockaddr_in6 *a = (void *)&cfg->cfg_grp;
            uint8_t *b = (void *)&(a->sin6_addr);
            if (b[0] == 0xff && (b[1] & 0xf0) == 0x30) {
                ssm_group = 1;
            }
        } else {
            /* RFC 4607 says IPv4 SSM groups are in 232/8 */
            struct sockaddr_in *a = (void *)&cfg->cfg_grp;
            uint8_t *b = (void *)&(a->sin_addr);
            if (b[0] == 232) {
                ssm_group = 1;
            }
        }

        /* are we trying to do source-specific multicast? */
        ssm_filter = (cfg->cfg_sfmode == MCAST_INCLUDE);

        /* well, is that ok? */
        if (ssm_group && !ssm_filter) {
            errout("warning: %s is a source specific multicast group",
                   auto_ntop(&cfg->cfg_grp, gbuf, sizeof(gbuf)));
        }
        if (ssm_filter && !ssm_group) {
            errout("warning: %s is not a source specific multicast group",
                   auto_ntop(&cfg->cfg_grp, gbuf, sizeof(gbuf)));
        }
    }
#endif /* DO_SOURCES */
}

/** ** utility functions ** **/

/*
 * errout() - Emit an error message with timestamp.
 * Takes a printf()-style format string.
 */
static void errout(char *fmt, ...)
{
    va_list ap;
    char buf[512];
    int pos;
    struct timeval tv;

    /* timestamp */
    gettimeofday(&tv, NULL);
    pos = (*timestamp_formatter)(&tv, buf, sizeof(buf),
                                 timestamp_formatter_arg);

    /* delimiter */
    if (pos && pos < sizeof(buf) - 1) {
        buf[pos++] = ' ';
        buf[pos] = '\0';
    }

    /* message */
    if (pos < sizeof(buf)) {
        va_start(ap, fmt);
        pos += vsnprintf(buf + pos, sizeof(buf) - pos, fmt, ap);
        va_end(ap);
    }

    /* newline and null byte */
    if (pos < sizeof(buf)) {
        buf[pos++] = '\n';
        if (pos >= sizeof(buf)) {
            pos = sizeof(buf) - 1;
        }
        buf[pos] = '\0';
    }

    /* out */
    fputs(buf, stderr);
}

/*
 * errthrottle()
 * Some errors might result in an infinite loop.  This function should help
 * make them less of a pain.  Normally when it's called it does nothing.
 * But if it's called a lot of times within a short period it'll sleep,
 * slowing things down.
 */
static void errthrottle(void)
{
    static int errthrottle_ctr = 0; /* calls in 64 second period */
    static time_t errthrottle_lst = 0; /* last 64 second period called */
    int prd;

    /* what time is it? use 64 second periods for convenience */
    prd = time(NULL) >> 6;

    /* how many times has errthrottle() been called in that time? */
    if (errthrottle_lst != prd) {
        errthrottle_lst = prd;
        errthrottle_ctr = 0;
    }
    ++errthrottle_ctr;

    /* sleep if it's a lot */
    if (errthrottle_ctr > 20) {
        sleep(1);
    }
}

/*
 * emit()
 * Emit a line of our main output.
 * Parameters:
 *      cfg - configuration
 *      evt - event: what happened; one of enum reported_events
 *      extra - extra information if any
 */
static void emit(struct config *cfg, enum reported_events evt, char *extra)
{
    struct timeval tv;
    char ts[128];
    char *ekw, *eph, *eex;

    /*
     * Decide whether 'evt' is an event we're reporting now,
     * and what to call it.
     */
    switch (evt) {
    case reported_event_tx:
        /* transmitted packet: only report if verbose */
        if (cfg->cfg_verbose == 0) return;
        ekw = "sent";
        eph = "sent packet to";
        break;
    case reported_event_rx:
        /* received packet: only report if verbose */
        if (cfg->cfg_verbose == 0) return;
        ekw = "recv";
        eph = "received packet on";
        break;
    case reported_event_up:
        /*
         * Coming "up" after receiving a packet: *don't* report if verbose,
         * because then reported_event_rx shows up.
         * (except with doubly-verbose)
         */
        if (cfg->cfg_verbose == 1) return;
        ekw = "up";
        eph = "started receiving packets on";
        break;
    case reported_event_dn:
        /* timeout after not receiving a packet: *don't* report if verbose,
         * because then the absence of reported_event_rx should be clear
         * enough.
         * (except with doubly-verbose)
         */
        if (cfg->cfg_verbose == 1) return;
        ekw = "down";
        eph = "no longer receiving packets on";
        break;
    case reported_event_cmd:
        /* command issued on stdin; see -k */
        ekw = "command";
        eph = "received command for";
        break;
    case reported_event_note:
        /* informational note */
        ekw = "note";
        eph = "note:";
        break;
    default:
        /* unknown event, don't report it */
        return;
    }

    /* fill in a timestamp */
    gettimeofday(&tv, NULL);
    (*timestamp_formatter)(&tv, ts, sizeof(ts), timestamp_formatter_arg);

    /* message */
    if (cfg->cfg_csv) {
        /* comma separated values format: time, label, keyword, extra */
        eex = extra ? csv_escape(extra) : NULL;
        printf("%s%s%s,%s,%s\n",
               ts, ts[0] ? "," : "",
               cfg->cfg_label_csv, ekw, eex ? : "");
        if (eex) {
            free(eex);
        }
    } else {
        /* more or less human readable format */
        printf("%s%s%s %s%s%s\n",
               ts, ts[0] ? " " : "", eph, cfg->cfg_label,
               extra ? " " : "", extra ? : "");
    }

    /* write out the message right away */
    fflush(stdout);
}

/*
 * auto_pton()
 *
 * Parse an IP address.  Determines from its contents whether it's IPv4
 * or IPv6.
 *
 * Parameters:
 *      s - the address string to parse
 *      ss - structure to store it in
 *      sslen - before: size of 'ss'; after: size actually used in 'ss'
 *      cfg - config structure; cfg->cfg_af will be updated/enforced, so
 *          that this program accepts only all-IPv4 or all-IPv6.
 * Return:
 *      !=0 on success
 *      0 on failure
 */
static int auto_pton(char *s, struct sockaddr_storage *ss,
                     socklen_t *sslen, struct config *cfg)
{
    int rv;
    void *ap;

    if (cfg->cfg_af == AF_UNSPEC) {
        /*
         * Figure out what address family this is.  Simple idea: ':'
         * appears in IPv6 addresses, not in IPv4.
         */
        if (strchr(s, ':')) {
            cfg->cfg_af = AF_INET6;
        } else {
            cfg->cfg_af = AF_INET;
        }
    }
    memset(ss, 0, sizeof(*ss));
    ss->ss_family = cfg->cfg_af;
    if (cfg->cfg_af == AF_INET6) {
        if (*sslen < sizeof(struct sockaddr_in6)) {
            errout("internal error: trying to fit IPv6 address in short space");
            return(0);
        }
        *sslen = sizeof(struct sockaddr_in6);
        ap = &((struct sockaddr_in6 *)ss)->sin6_addr;
        ((struct sockaddr_in6 *)ss)->sin6_port = 0;
#ifdef HAVE_SA_LEN
        ((struct sockaddr_in6 *)ss)->sin6_len = sizeof(struct sockaddr_in6);
#endif
    } else {
        if (*sslen < sizeof(struct sockaddr_in)) {
            errout("internal error: trying to fit IPv4 address in short space");
            return(0);
        }
        *sslen = sizeof(struct sockaddr_in);
        ap = &((struct sockaddr_in *)ss)->sin_addr;
        ((struct sockaddr_in *)ss)->sin_port = 0;
#ifdef HAVE_SA_LEN
        ((struct sockaddr_in *)ss)->sin_len = sizeof(struct sockaddr_in);
#endif
    }
    rv = inet_pton(cfg->cfg_af, s, ap);
    if (rv > 0) {
        return(1); /* success */
    } else if (rv == 0) {
        errout("Invalid %s address '%s'",
               (cfg->cfg_af == AF_INET6) ? "IPv6" : "IPv4", s);
        return(0);
    } else {
        errout("Error in parsing address '%s': %s", s, strerror(errno));
        return(0);
    }
}

/*
 * auto_ntop()
 *
 * Format an IPv4 or IPv6 address.  Determines from its contents whether
 * it's IPv4 or IPv6.  Wrapper around inet_ntop().
 *
 * Parameters:
 *      src - the address (pointer to a 'struct sockaddr*')
 *      dst - buffer to store the result
 *      size - size of dst in bytes
 * Return:
 *      pointer to the string in dst
 */
static char *auto_ntop(void *src, char *dst, socklen_t size)
{
    int af;

    /* what kind of address is it? */
    if (src == NULL) {
        af = AF_UNSPEC;
    } else {
        af = ((struct sockaddr *)src)->sa_family;
    }

    /* format this kind of address */
    switch (af) {
    case AF_INET:
        /* IPv4 */
        if(!inet_ntop(af, &((struct sockaddr_in *)src)->sin_addr, dst, size)) {
            snprintf(dst, size, "?");
        }
        break;
    case AF_INET6:
        /* IPv6 */
        if(!inet_ntop(af, &((struct sockaddr_in6 *)src)->sin6_addr, dst, size)){
            snprintf(dst, size, "?");
        }
        break;
    default:
        /* unsupported, or maybe there's no address */
        snprintf(dst, size, "?");
        break;
    }

    return(dst);
}

#ifdef DO_SOURCES
/*
 * sort_addrs()
 *
 * Sort an array of addresses into a straightforward consistent order.
 * That makes comparisons and merges easier.
 *
 * Parameters:
 *      addrs - the addresses
 *      naddrs - number of entries in 'addrs'
 */
static void sort_addrs(struct sockaddr_storage *addrs, int naddrs)
{
    qsort(addrs, naddrs, sizeof(addrs[0]), &sort_addrs_compare);
}

/*
 * sort_addrs_compare()
 *
 * Compare two addresses and return <0, =0, >0 depending on their
 * relationship.  This is used with qsort(), by sort_addrs(), to put
 * addresses in a consistent order.
 *
 * It handles having addresses of different family, or NULL, even though
 * this program avoids the former, and qsort() itself should avoid the latter.
 *
 * The addresses are stored in a 'struct sockaddr_storage' but this function
 * only compares the address part, not the port, flow, or scope.
 */
static int sort_addrs_compare(const void *x, const void *y)
{
    const struct sockaddr_storage *xx = x;
    const struct sockaddr_storage *yy = y;

    if (xx == NULL) {
        if (yy == NULL) {
            return(0);
        } else {
            return(-1);
        }
    } else if (yy == NULL) {
        return(1);
    } else if (xx->ss_family < yy->ss_family) {
        return(-1);
    } else if (xx->ss_family > yy->ss_family) {
        return(1);
    } else if (xx->ss_family == AF_INET) {
        const struct sockaddr_in *x4 = x;
        const struct sockaddr_in *y4 = y;
        return(memcmp(&(x4->sin_addr), &(y4->sin_addr),
                      sizeof(x4->sin_addr)));
    } else if (yy->ss_family == AF_INET6) {
        const struct sockaddr_in6 *x6 = x;
        const struct sockaddr_in6 *y6 = y;
        return(memcmp(&(x6->sin6_addr), &(y6->sin6_addr),
                      sizeof(x6->sin6_addr)));
    } else {
        /* shouldn't happen */
        return(0);
    }
}

/*
 * add_addrs()
 *
 * Given two arrays of addresses, allocate and fill in a new one that
 * contains everything the two inputs have, without any duplicates.
 * Expects, and maintains, the ordering within arrays that sort_addrs()
 * establishes.
 *
 * Parameters:
 *      res_addrs - the newly allocated resulting array will be stored here
 *      res_naddrs - the size of res_addrs will be stored here
 *      lft_addrs - one of the input arrays
 *      lft_naddrs - size of lft_addrs
 *      rgt_addrs - the other input array
 *      rgt_naddrs - size of rgt_addrs
 */
static void add_addrs(struct sockaddr_storage **res_addrs, int *res_naddrs,
                      struct sockaddr_storage *lft_addrs, int lft_naddrs,
                      struct sockaddr_storage *rgt_addrs, int rgt_naddrs)
{
    int i, j, o, c;
    struct sockaddr_storage *last = NULL;

    /* allocate *res_addrs[] as max size it could be */
    *res_addrs = calloc(lft_naddrs + rgt_naddrs, sizeof((*res_addrs)[0]));

    /* go through the arrays */
    for (i = j = o = 0; i < lft_naddrs || j < rgt_naddrs; ) {
        /* which comes first in order? */
        if (i >= lft_naddrs) {
            c = 1; /* nothing left on left, use right */
        } else if (j >= rgt_naddrs) {
            c = -1; /* nothing left on right, use left */
        } else {
            c = sort_addrs_compare(&lft_addrs[i], &rgt_addrs[j]);
        }

        /* if it's not a duplicate, take it; if it is, skip it */
        if (c <= 0) {
            if (last == NULL || 0 != sort_addrs_compare(&lft_addrs[i], last)) {
                /* not duplicate */
                memcpy(&(*res_addrs)[o], &lft_addrs[i],
                       sizeof((*res_addrs)[o]));
                last = &(*res_addrs)[o];
                ++o;
            }
            ++i;
        } else {
            if (last == NULL || 0 != sort_addrs_compare(&rgt_addrs[j], last)) {
                /* not duplicate */
                memcpy(&(*res_addrs)[o], &rgt_addrs[j],
                       sizeof((*res_addrs)[o]));
                last = &(*res_addrs)[o];
                ++o;
            }
            ++j;
        }
    }
    *res_naddrs = o;
}

/*
 * sub_addrs()
 * 
 * Given two arrays of addresses, allocate and fill in a new one that
 * contains everything the left input has, without anything the right
 * input has. Expects, and maintains, the ordering within arrays that
 * sort_addrs() establishes.
 *
 * Parameters:
 *      res_addrs - the newly allocated resulting array will be stored here
 *      res_naddrs - the size of res_addrs will be stored here
 *      inc_addrs - the input array with stuff to include
 *      inc_naddrs - size of lft_addrs
 *      exc_addrs - the input array with stuff to exclude
 *      exc_naddrs - size of rgt_addrs
 */
static void sub_addrs(struct sockaddr_storage **res_addrs, int *res_naddrs,
                      struct sockaddr_storage *inc_addrs, int inc_naddrs,
                      struct sockaddr_storage *exc_addrs, int exc_naddrs)
{
    int i, j, o, c;

    /* allocate *res_addrs[] as max size it could be */
    *res_addrs = calloc(inc_naddrs, sizeof((*res_addrs)[0]));

    for (i = j = o = 0; i < inc_naddrs && j < exc_naddrs; ) {
        /* which comes first in order? */
        c = sort_addrs_compare(&inc_addrs[i], &exc_addrs[j]);

        if (c < 0) {
            /* something included, not excluded */
            memcpy(&((*res_addrs)[o]), &inc_addrs[i], sizeof((*res_addrs)[o]));
            ++i;
            ++o;
        } else if (c > 0) {
            /* something excluded, wasn't there anyways */
            ++j;
        } else if (c == 0) {
            /* something in both lists, leave it out */
            ++i;
        }
    }
    if (i < inc_naddrs) {
        /* a bunch of things to just include */
        memcpy(&((*res_addrs)[o]), &inc_addrs[i],
               sizeof((*res_addrs)[o]) * (inc_naddrs - i));
        o += inc_naddrs - i;
    }
    *res_naddrs = o;
}
#endif /* DO_SOURCES */

/*
 * csv_escape()
 * Returns a copy of 's' with quotes and escapes added to make any odd
 * characters safe for inclusion in a single field of a CSV file.  The
 * returned string is allocated memory and should be freed by the caller
 * when no longer needed.
 *
 * See also RFC 4180.
 */
static char *csv_escape(char *s)
{
    int i, o;
    int do_escape = 0;
    char *r;

    /* any characters that require escaping? */
    for (i = 0; s[i]; ++i) {
        if (!isascii(s[i]) || !isgraph(s[i]) ||
            s[i] == '"' || s[i] == ',') {

            do_escape = 1;
            break;
        }
    }

    if (do_escape) {
        /* wrap in quotes and double any quotes */
        r = malloc(strlen(s) * 2 + 3); /* maximum possible */
        o = 0;
        r[o++] = '"';
        for (i = 0; s[i]; ++i) {
            if (s[i] == '"') {
                r[o++] = '"';
            }
            r[o++] = s[i];
        }
        r[o++] = '"';
        r[o] = '\0';
        return(r);
    } else {
        /* no need to change anything */
        return(strdup(s));
    }
}

/** ** timestamp formatters ** **/

/*
 * timestamp_log()
 * Fill in a timestamp like: Sep 12 00:01:17.123
 * Parameters:
 *      tv - time to use as input
 *      buf - buffer to store output
 *      len - size of buffer
 *      arg - ignored; will be a copy of timestamp_formatter_arg
 * Returns:
 *      length of resulting string
 */
static int timestamp_log(struct timeval *tv, char *buf, size_t len, void *arg)
{
    struct tm *tm;
    size_t pos;

    tm = localtime(&tv->tv_sec);
    pos = strftime(buf, len, "%b %d %H:%M:%S", tm);
    if (pos > 0 && pos < len) {
        pos += snprintf(buf + pos, len - pos, ".%03u",
                        (unsigned)(tv->tv_usec / 1000));
    }
    if ((pos == 0 || pos >= len) && len > 0) {
        /* the string didn't fit; this shouldn't happen */
        buf[pos = 0] = '\0';
    }
    return(pos);
}

/*
 * timestamp_raw()
 * Fill in a timestamp like: 1599943404.456
 * Parameters:
 *      tv - time to use as input
 *      buf - buffer to store output
 *      len - size of buffer
 *      arg - ignored; will be a copy of timestamp_formatter_arg
 * Returns:
 *      length of resulting string
 */
static int timestamp_raw(struct timeval *tv, char *buf, size_t len, void *arg)
{
    int pos = snprintf(buf, len, "%lu.%03u",
                       (unsigned long)tv->tv_sec,
                       (unsigned)(tv->tv_usec / 1000));
    if (pos >= len && len > 0) {
        /* the string didn't fit; this shouldn't happen */
        buf[pos = 0] = '\0';
    }
    return(pos);
}

/*
 * timestamp_num()
 * Fill in a timestamp like: 2020-09-12-13:46:43.789
 * Parameters:
 *      tv - time to use as input
 *      buf - buffer to store output
 *      len - size of buffer
 *      arg - ignored; will be a copy of timestamp_formatter_arg
 * Returns:
 *      length of resulting string
 */
static int timestamp_num(struct timeval *tv, char *buf, size_t len, void *arg)
{
    struct tm *tm;
    size_t pos;

    tm = localtime(&tv->tv_sec);
    pos = strftime(buf, len, "%Y-%m-%d-%H:%M:%S", tm);
    if (pos > 0 && pos < len) {
        pos += snprintf(buf + pos, len - pos, ".%03u",
                        (unsigned)(tv->tv_usec / 1000));
    }
    if ((pos == 0 || pos >= len) && len > 0) {
        /* the string didn't fit; this shouldn't happen */
        buf[pos = 0] = '\0';
    }
    return(pos);
}

/*
 * timestamp_none()
 * Don't fill in a timestamp, just empty string.
 * Parameters:
 *      tv - time to use as input
 *      buf - buffer to store output
 *      len - size of buffer
 *      arg - ignored; will be a copy of timestamp_formatter_arg
 * Returns:
 *      length of resulting string
 */
static int timestamp_none(struct timeval *tv, char *buf, size_t len, void *arg)
{
    if (len > 0) {
        buf[0] = '\0';
    }
    return(0);
}

/** ** main program ** **/

/*
 * main() - main program, run from the command line
 */
int main(int argc, char **argv)
{
    struct config main_cfg, *cfg;
    int oc, sok, rv;
    int recompute_timeout, reapply_filter, filter_critical;
    int rx_state_up = 0;
    enum command_action ca;
    char empty[1];
    uint32_t rxpkt[512];
    struct sockaddr_storage as, dsta;
    struct sockaddr_in *a4;
    struct sockaddr_in6 *a6;
    struct timeval tnow, tsel, tlast;
    socklen_t alen, dstalen;
    fd_set rfds;
    long tflat;

    /* figure out program name & what it implies as to functionality */
    if (argc > 0) {
        char *bn;

        /* program name on command line */
        bn = basename(argv[0]);
        if (bn && *bn) {
            progname = bn;
        }
    }
    progname_to_progdir();

    /* default config and other initializations */
    memset(&main_cfg, 0, sizeof(main_cfg));
    cfg = &main_cfg;
    main_cfg.cfg_dir = progdir;
    main_cfg.cfg_af = AF_UNSPEC;
    main_cfg.cfg_grp.ss_family = AF_UNSPEC;
    main_cfg.cfg_grplen = sizeof(main_cfg.cfg_grp);
    main_cfg.cfg_port = 0;
    main_cfg.cfg_intf.nam[0] = '\0';
    main_cfg.cfg_intf.idx = 0;
    main_cfg.cfg_intf.adr.s_addr = INADDR_ANY;
    main_cfg.cfg_ttl = DEF_TTL;
    main_cfg.cfg_verbose = 0;
    main_cfg.cfg_label = NULL;
    main_cfg.cfg_label_csv = NULL;
    main_cfg.cfg_csv = 0;
    main_cfg.cfg_period = 1.0;
    main_cfg.cfg_period_us = 1000000;
    main_cfg.cfg_multiplier = 3.0;
    main_cfg.cfg_timeout_us = 3000000;
    main_cfg.cfg_data = NULL;
    main_cfg.cfg_data_len = 0;
    main_cfg.cfg_join = 0;
    main_cfg.cfg_command_in = 0;
    main_cfg.cfg_command_got = 0;
    main_cfg.cfg_command_ignore = 0;
#ifdef DO_SOURCES
    main_cfg.cfg_sfmode = MCAST_EXCLUDE;
    main_cfg.cfg_sources = calloc(1, sizeof(main_cfg.cfg_sources[0]));
    main_cfg.cfg_nsources = 0;
    main_cfg.cfg_osfmode = MCAST_EXCLUDE;
    main_cfg.cfg_osources = calloc(1, sizeof(main_cfg.cfg_osources[0]));
    main_cfg.cfg_onsources = 0;
#endif /* DO_SOURCES */
    main_cfg.cfg_sml_state.ever_called = 0;

    gettimeofday(&tnow, NULL);
    tlast = tnow;

    /* parse the command line options */
    while ((oc = getopt(argc, argv, "trg:p:i:T:E:I:vl:f:P:m:d:jk")) >= 0) {
        if (oc == '?') {
            errout("unrecognized command line option");
            usage();
            exit(1);
        } else {
            empty[0] = '\0';
            ca = option(&main_cfg, '\0', oc, optarg ? : empty);
        }
        if (ca == command_action_error) {
            exit(1);
        }
    }
    if (optind != argc) {
        errout("too many arguments");
        usage();
        exit(1);
    }
    recompute_timeout = reapply_filter = filter_critical = 1;

    /* sanity checks and adjustments to the configuration */
    if (cfg->cfg_dir == 0) {
        errout("am I sending or receiving? specify -t or -r");
        exit(1);
    }
    if (cfg->cfg_grp.ss_family == AF_UNSPEC) {
        /* default multicast group */
        cfg->cfg_grplen = sizeof(cfg->cfg_grp);
        if (cfg->cfg_af == AF_INET6) {
            auto_pton(DEF_IPV6_GROUP, &cfg->cfg_grp, &cfg->cfg_grplen, cfg);
        } else {
            auto_pton(DEF_IPV4_GROUP, &cfg->cfg_grp, &cfg->cfg_grplen, cfg);
        }
    }
    if (cfg->cfg_port == 0) {
        /* default port number */
        cfg->cfg_port = DEF_UDP_PORT; /* port number */
    }
    if (cfg->cfg_intf.nam[0] == '\0') {
        errout("what network interface? specify -i");
        exit(1);
    }
    group_check(cfg, 1);

    /* set up the socket which is used for all network interactions */
    sok = socket(cfg->cfg_af, SOCK_DGRAM, IPPROTO_UDP);
    if (sok < 0) {
        errout("failed to create socket: %s", strerror(errno));
        exit(1);
    }

    if (cfg->cfg_dir < 0) {
        /* avoid EADDRINUSE */
        int arg = 1;
        rv = setsockopt(sok, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg));
        if (rv < 0) {
            errout("failed to set SO_REUSEADDR: %s", strerror(errno));
            /* go on and try, in spite of this error */
        }
    }

    if (cfg->cfg_dir < 0) {
        /* to receive packets we have to bind() the socket */
        memset(&as, 0, sizeof(as));
        if (cfg->cfg_af == AF_INET6) {
            a6 = (void *)&as;
            a6->sin6_family = AF_INET6;
            a6->sin6_port = htons(cfg->cfg_port);
            a6->sin6_addr = in6addr_any;
            alen = sizeof(*a6);
        } else {
            a4 = (void *)&as;
            a4->sin_family = AF_INET;
            a4->sin_port = htons(cfg->cfg_port);
            a4->sin_addr.s_addr = INADDR_ANY;
            alen = sizeof(*a4);
        }
        rv = bind(sok, (void *)&as, alen);
        if (rv < 0) {
            errout("failed to bind socket: %s", strerror(errno));
            exit(1);
        }
    }

    if (cfg->cfg_dir < 0) {
#ifdef HAVE_MULTICAST_ALL
        if (cfg->cfg_af == AF_INET) {
            /* don't receive packets for groups other sockets joined */
            int arg = 0;
            rv = setsockopt(sok, IPPROTO_IP, IP_MULTICAST_ALL,
                            &arg, sizeof(arg));
            if (rv < 0) {
                errout("failed to set IP_MULTICAST_ALL to False: %s",
                       strerror(errno));
                /* go on and try, in spite of this error */
            }
        }
#endif /* HAVE_MULTICAST_ALL */
#ifdef HAVE_V6_MULTICAST_ALL
        if (cfg->cfg_af == AF_INET6) {
            /* don't receive packets for groups other sockets joined */
            int arg = 0;
            rv = setsockopt(sok, IPPROTO_IPV6, IPV6_MULTICAST_ALL,
                            &arg, sizeof(arg));
            if (rv < 0) {
                errout("failed to set IPV6_MULTICAST_ALL to False: %s",
                       strerror(errno));
                /* go on and try, in spite of this error */
            }
        }
#endif /* HAVE_V6_MULTICAST_ALL */
    }

    if (cfg->cfg_dir > 0) {
        if (cfg->cfg_ttl >= 0) {
            /* specify time to live / hop limit value, when sending */
            if (cfg->cfg_af == AF_INET6) {
                int arg = cfg->cfg_ttl;
                rv = setsockopt(sok, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                                &arg, sizeof(arg));
                if (rv < 0) {
                    errout("failed to set IPV6_MULTICAST_HOPS to %d: %s",
                           (int)cfg->cfg_ttl, strerror(errno));
                    /* this error is not fatal; go on */
                }
            } else {
                int arg = cfg->cfg_ttl;
                rv = setsockopt(sok, IPPROTO_IP, IP_MULTICAST_TTL,
                                &arg, sizeof(arg));
                if (rv < 0) {
                    errout("failed to set IP_MULTICAST_TTL to %d: %s",
                           (int)cfg->cfg_ttl, strerror(errno));
                    /* go on and try, in spite of this error */
                }
            }
        }
    }

    /* attach to a network interface */
    if (cfg->cfg_af == AF_INET6) {
        /* IPV6_MULTICAST_IF takes ifindex */
        int arg = cfg->cfg_intf.idx;
        rv = setsockopt(sok, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                        &arg, sizeof(arg));
        if (rv < 0) {
            errout("failed to set IPV6_MULTICAST_IF to %d (%s): %s",
                   (int)cfg->cfg_intf.idx, cfg->cfg_intf.nam,
                   strerror(errno));
            /* go on and try, in spite of this error */
        }
    } else {
#ifdef HAVE_MULTICAST_IF_IP_MREQN
        /* IP_MULTICAST_IF can take a 'struct ip_mreqn' which can
         * hold an ifindex
         */
        struct ip_mreqn oarg;
        memset(&oarg, 0, sizeof(oarg));
        oarg.imr_ifindex = cfg->cfg_intf.idx;
#else /* HAVE_MULTICAST_IF_IP_MREQN */
        /* IP_MULTICAST_IF takes an interface address */
        struct in_addr oarg = cfg->cfg_intf.adr;
#endif /* HAVE_MULTICAST_IF_IP_MREQN */
        rv = setsockopt(sok, IPPROTO_IP, IP_MULTICAST_IF, &oarg, sizeof(oarg));
        if (rv < 0) {
            errout("failed to set IP_MULTICAST_IF: %s", strerror(errno));
            /* go on and try, in spite of this error */
        }
    }

    /* set up address to send to; for convenience set it up even if receiving */
    dsta = cfg->cfg_grp;
    dstalen = cfg->cfg_grplen;
    if (cfg->cfg_grp.ss_family == AF_INET6) {
        a6 = (void *)&dsta;
        a6->sin6_port = htons(cfg->cfg_port);
    } else {
        a4 = (void *)&dsta;
        a4->sin_port = htons(cfg->cfg_port);
    }

    /* figure out some default stuff */
    if (cfg->cfg_label == NULL) {
        cfg->cfg_label = make_default_label(cfg);
    }
    if (cfg->cfg_label_csv == NULL) {
        cfg->cfg_label_csv = csv_escape(cfg->cfg_label);
    }
    if (cfg->cfg_data == NULL) {
        cfg->cfg_data = malloc(8);
        ((uint32_t *)cfg->cfg_data)[0] = htonl(tnow.tv_sec);
        ((uint32_t *)cfg->cfg_data)[1] = htonl(tnow.tv_usec);
        cfg->cfg_data_len = 8;
    }

    /* main loop, where stuff actually happens */
    for (;;) {
        if (cfg->cfg_verbose > 2) {
            errout("top of main loop"); /* handy for debugging */
        }

        /* Handle changes to the timeout in the configuration */
        if (recompute_timeout) {
            recompute_timeout = 0;
            cfg->cfg_period_us = rint(cfg->cfg_period * 1e+6);
            cfg->cfg_timeout_us = rint(cfg->cfg_period * 1e+6 *
                                       cfg->cfg_multiplier);
        }

        /*
         * Handle changes to the source list; including joining the
         * multicast group.  Happens if we're the receiver; even if
         * we're not, in the case of the "-j" option.
         */
        if (cfg->cfg_dir > 0 && !cfg->cfg_join) {
            reapply_filter = 0;
        }
        if (reapply_filter) {
            reapply_filter = 0;
            rv = setup_mcast_listen(sok, &cfg->cfg_intf,
                                    (void *)&cfg->cfg_grp, cfg->cfg_grplen,
#ifdef DO_SOURCES
                                    cfg->cfg_sfmode, cfg->cfg_nsources,
                                    cfg->cfg_sources,
#endif /* DO_SOURCES */
                                    &cfg->cfg_sml_state);
            if (rv < 0) {
                errout("filter setting failed: %s", strerror(errno));
#ifdef DO_SOURCES
                if (filter_critical) {
                    exit(1);
                }
                /* instead of giving up, try to set back the old value */
                errthrottle();
                if (cfg->cfg_sources) {
                    free(cfg->cfg_sources);
                }
                cfg->cfg_sfmode = cfg->cfg_osfmode;
                cfg->cfg_nsources = cfg->cfg_onsources;
                cfg->cfg_sources = calloc(cfg->cfg_nsources,
                                          sizeof(cfg->cfg_sources[0]));
                memcpy(cfg->cfg_sources,
                       cfg->cfg_osources,
                       cfg->cfg_nsources * sizeof(cfg->cfg_sources[0]));
                reapply_filter = filter_critical = 1;
                continue;
#else /* DO_SOURCES */
                /* no tricky retries when we can't even join the group */
                exit(1);
#endif /* !DO_SOURCES */
            } else {
#ifdef DO_SOURCES
                /* the target/new settings have become the current/old ones */
                if (cfg->cfg_osources) {
                    free(cfg->cfg_osources);
                }
                cfg->cfg_osfmode = cfg->cfg_sfmode;
                cfg->cfg_onsources = cfg->cfg_nsources;
                cfg->cfg_osources = calloc(cfg->cfg_nsources,
                                           sizeof(cfg->cfg_osources[0]));
                memcpy(cfg->cfg_osources,
                       cfg->cfg_sources,
                       cfg->cfg_nsources * sizeof(cfg->cfg_osources[0]));
#endif /* DO_SOURCES */
            }
        }

        /*
         * Figure out what to wait for -- input, timeout.
         * "rfds" gets bits for the inputs to monitor.
         * "tsel" gets the time to wait.
         * "tflat" temporarily holds the time in the more convenient
         * form of a single number.  Near the top it's the time that's passed
         * since the last thing happened.  Further down it's the time until
         * next time.  Either way it's a number of microseconds.
         */
        FD_ZERO(&rfds);
        gettimeofday(&tnow, NULL);
        tflat = tnow.tv_sec - tlast.tv_sec;
        tflat *= 1000000;
        tflat += tnow.tv_usec - tlast.tv_usec;
        if (tflat < 0) {
            /* Time has gone backwards.  Or at least the clock. */
            tflat = 0;
            tlast = tnow;
        }
        if (cfg->cfg_command_in) {
            /* listening for commands, if enabled */
            FD_SET(STDIN_FILENO, &rfds);
        }
        if (cfg->cfg_dir < 0) {
            /* receive (-r) mode: listen for packets & wait for timeout */
            FD_SET(sok, &rfds);
            if (rx_state_up) {
                tflat = cfg->cfg_timeout_us - tflat;
            } else {
                /* too late to time out; just wait for packets */
                tflat = 1800000000L; /* half an hour */
            }
        } else {
            /* transmit (-t) mode: wait for next send time */
            tflat = cfg->cfg_period_us - tflat;
        }
        if (tflat < 0) {
            /* We missed it. Hopefully not by much.  Better go *now.* */
            tflat = 0;
        }
        tsel.tv_sec = tflat / 1000000;
        tsel.tv_usec = tflat % 1000000;

        /* if it's already time to do something, do it */
        if (tflat == 0) {
            if (cfg->cfg_dir < 0 && rx_state_up) {
                /* receive (-r) mode: time out */
                rx_state_up = 0;
                emit(cfg, reported_event_dn, NULL);
            }
            if (cfg->cfg_dir > 0) {
                /* send (-t) mode: send a packet */
                rv = sendto(sok, cfg->cfg_data, cfg->cfg_data_len,
                            0, (void *)&dsta, dstalen);
                if (rv < 0) {
                    errout("sendto() failed: %s", strerror(errno));
                } else {
                    emit(cfg, reported_event_tx, NULL);
                }
                tlast = tnow;
            }
        }

        /* wait until there's something to do */
        rv = select(sok + 1, &rfds, NULL, NULL, &tsel);

        /* and how did that turn out? */
        if (rv < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                /* nothing really wrong, just go around again */
            } else {
                /* some kind of error */
                errout("select() error: %s", strerror(errno));
                errthrottle();
            }
            continue;
        } else {
            if (cfg->cfg_command_in && FD_ISSET(STDIN_FILENO, &rfds)) {
                /* read command data into the buffer */
                rv = read(STDIN_FILENO,
                          &cfg->cfg_command_buf[cfg->cfg_command_got],
                          sizeof(cfg->cfg_command_buf) - cfg->cfg_command_got);
                if (rv < 0) {
                    if (errno == EAGAIN || errno == EINTR ||
                        errno == EWOULDBLOCK) {

                        /* nothing really happened */
                    } else {
                        /* error; don't let it happen again */
                        errout("treating error on stdin (%s) as implicit +k",
                               strerror(errno));
                        cfg->cfg_command_in = 0;
                        cfg->cfg_command_ignore = 0;
                    }
                }
                else if (rv == 0) {
                    /* end of file */
                    errout("end of command input: implicit +k");
                    cfg->cfg_command_in = 0;
                    cfg->cfg_command_ignore = 0;
                } else {
                    cfg->cfg_command_got += rv;
                }
            } else if (cfg->cfg_dir < 0 && FD_ISSET(sok, &rfds)) {
                /* receive a packet */
                rv = recv(sok, rxpkt, sizeof(rxpkt), 0);
                if (rv < 0) {
                    /* packet not received */
                    if (errno == EAGAIN || errno == EWOULDBLOCK ||
                        errno == EINTR) {
                        /* not really an error */
                    } else {
                        errout("recv() failed: %s", strerror(errno));
                        errthrottle();
                    }
                } else {
                    /* packet received */
                    gettimeofday(&tlast, NULL);
                    emit(cfg, reported_event_rx, NULL);
                    if (!rx_state_up) {
                        rx_state_up = 1;
                        emit(cfg, reported_event_up, NULL);
                    }
                }
            }
        }

        /* handle any commands that came in on stdin */
        while (cfg->cfg_command_in && cfg->cfg_command_got > 0) {
            ca = command(cfg);
            if (ca == command_action_wait) {
                /* no complete lines left */
                break;
            }
            switch (ca) {
            case command_action_none:
                /* nothing more to do */
                break;
            case command_action_error:
                /* error occurred, already reported; nothing more to do */
                break;
    #ifdef DO_SOURCES
            case command_action_source:
                /* source filter mode / list change */
                group_check(cfg, 0);
                reapply_filter = 1;
                filter_critical = 0;
                break;
    #endif /* DO_SOURCES */
            case command_action_exit_program:
                /* end the program */
                errout("exiting on command");
                exit(0);
                break;
            case command_action_time_change:
                /* period/multiplier/timeout changed */
                recompute_timeout = 1;
                break;
            default:
            case command_action_wait:
                /* nothing more to do */
                break;
            }
        }
    }
}
