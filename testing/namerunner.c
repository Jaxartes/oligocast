/*
 * namerunner.c
 * A test program for one little part of oligocast: processing the
 * command name (argv[0]).  Interpreting the results is left up to
 * another program, namerunner_parse.py.
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <libgen.h>

/** ** ** hard coded config and test data ** ** **/

/* path name of oligocast executable file */
char *exe = "./oligocast";

/* path name endings for the test & what they mean */
struct {
    char *end;
    int dir;
} ends[] = {
    { "send", 1 }, { "receive", -1 }, { "recv", -1 }, { "snd", 1 },
    { "rc", 0 }, { "receivex", 0 },
    { "rcv", -1 }, { "tx", 1 }, { "rx", -1 }, { "semd", 0 },
    { "", 0 }, { "!", 0 },
    { "rec", 0 }, { "xx", 0 }, { "thing", 0 }, { "rcw", 0 },
    { NULL, 0 }
};

/* stuff that can go before the command ending */
char *pfxs[] = { "abc", "/", "xyz", NULL };

/* and after */
char *sfxs[] = { ".send", ".exe", ".recv", ".x", NULL };

/** ** ** main ** ** **/

int main(int argc, char **argv)
{
    int e, s, p, l, i, dosuf, r, c;
    char name[512];
    char *args[3];
    pid_t pid, pjd;

    if (argc > 1) {
        c = atoi(argv[1]); /* repetition count */
    } else {
        c = 1;
    }

    /* Initialize the random number generator.  This is not a secure way
     * to do it, but it's just for a test.
     */
    srand48(time(NULL) ^ (getpid() * 769));

    for (; c > 0; --c) {
        /* go through possibilities */
        for (e = 0; ends[e].end; ++e) {
            /* build a name */
            l = 0;
            name[l] = '\0';

            /* first an ignored prefix */
            while (drand48() < 0.7 && l * 2 < sizeof(name)) {
                p = 0;
                for (i = lrand48() % 1000; i >= 0; --i) {
                    ++p;
                    if (pfxs[p] == NULL) {
                        p = 0;
                    }
                }
                strcat(name + l, pfxs[p]);
                l += strlen(pfxs[p]);
            }

            /* maybe an extra dot, maybe not */
            if (drand48() < 0.7) {
                dosuf = l && drand48() < 0.4;
            } else {
                dosuf = 1;
                name[l++] = '.';
                name[l] = '\0';
            }

            /* and the ending that might mean something */
            strcat(name + l, ends[e].end);
            l += strlen(ends[e].end);

            /* and maybe an ignored suffix */
            if (dosuf) {
                s = 0;
                for (i = lrand48() % 1000; i >= 0; --i) {
                    ++s;
                    if (sfxs[s] == NULL) {
                        s = 0;
                    }
                }
                strcat(name + l, sfxs[s]);
                l += strlen(sfxs[s]);
            }

            /* ok, that's the name, now make it part of an arguments list */
            if (ends[e].end[0] == '!') {
                /* special case, empty args */
                args[0] = NULL;
                name[0] = '!';
                name[1] = '\0';
            } else {
                args[0] = name;
                args[1] = "-?"; /* cause usage() to run */
                args[2] = NULL;
            }

            /* announce what we're going to do */
            fprintf(stderr, "# exe=\"%s\", args[]= { ", exe);
            for (i = 0; args[i]; ++i) {
                fprintf(stderr, "\"%s\", ", args[i]);
            }
            fprintf(stderr, "NULL }\n");
            fprintf(stderr, "# expected direction: %d (%s); name: \"%s\"\n",
                    (int)ends[e].dir,
                    (ends[e].dir == 0) ? "either" :
                    ((ends[e].dir > 0) ? "send" : "recv"), basename(name));

            /* run it */
            pid = fork();
            if (pid < 0) {
                perror("# fork() failed");
                continue;
            } else if (pid == 0) {
                /* child process: execute the program */
                r = execv(exe, args);
                /* if execv() returns, it must have failed */
                perror("# execv() failed");
                _exit(1);
            }

            /* wait for it to finish */
            r = 0;
            while ((pjd = waitpid(pid, &r, 0)) < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    r = 0;
                    continue;
                } else {
                    perror("# waitpid() failed");
                    break;
                }
            }

            if (WIFEXITED(r)) {
                if (!WEXITSTATUS(r)) {
                    fprintf(stderr, "# exit status: ok (which is abnormal)\n");
                } else {
                    fprintf(stderr, "# exit status: %d\n", (int)WEXITSTATUS(r));
                }
            } else if (WIFSIGNALED(r)) {
                fprintf(stderr, "# exit signal: %d%s (abnormal)\n",
                (int)WTERMSIG(r),
                WCOREDUMP(r) ? " (core dumped)" : "");
            } else {
                fprintf(stderr, "# exit - not exited; stopped (abnormal)\n");
            }
        }
    }

    fprintf(stderr, "# done\n");

    return(0);
}

