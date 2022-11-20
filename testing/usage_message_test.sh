#!/bin/sh
# Runs "oligocast" a bunch of times to get usage message in various cases.
# Command line arguments:
#       oligocast executable path
#       output file prefix

oligocast=$1
prefix=$2

$oligocast -h > ${prefix}_h_out 2> ${prefix}_h_err
$oligocast aitch > ${prefix}_nonopt_out 2> ${prefix}_nonopt_err
$oligocast -Q > ${prefix}_badopt_out 2> ${prefix}_badopt_err
( echo -h ; echo .x ) |
$oligocast -krilo -g225.0.0.1 -p22501 > ${prefix}_kh_out 2> ${prefix}_kh_err
$oligocast -vvh > ${prefix}_vvh_out 2> ${prefix}_vvh_err
( echo -h ; echo .x ) |
$oligocast -vvkrilo -g225.0.0.1 -p22501 > ${prefix}_vvkh_out 2> ${prefix}_vvkh_err
