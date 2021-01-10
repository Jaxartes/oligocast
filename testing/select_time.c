/* test out select() interaction of signals and timeout */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

int main(int argc, char **argv)
{
    fd_set rfds;
    float interval;
    struct timeval iv, jv, kv;
    int rv, e;

    if (argc != 2) {
        fprintf(stderr, "usage: select_time number_of_seconds\n");
        exit(1);
    }

    interval = atof(argv[1]);

    FD_ZERO(&rfds);
    iv.tv_sec = floor(interval);
    iv.tv_usec = floor((interval - iv.tv_sec) * 1e+6);
    gettimeofday(&jv, NULL);
    rv = select(1, &rfds, NULL, NULL, &iv);
    e = errno;
    gettimeofday(&kv, NULL);
    interval = kv.tv_sec - jv.tv_sec;
    interval += (kv.tv_usec - jv.tv_usec) / 1e+6;
    printf("select() returned %d%s%s%s in %.6g seconds\n",
           (int)rv,
           (rv < 0) ? " (" : "",
           (rv < 0) ? strerror(e) : "",
           (rv < 0) ? ")" : "",
           (float)interval);

    return(0);
}
