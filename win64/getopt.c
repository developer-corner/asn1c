/* Derived work from: */

/*	$NetBSD: getopt.c,v 1.26 2003/08/07 16:43:40 agc Exp $	*/

/*
 * Copyright (c) 1987, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* getopt.c */

#include "getopt.h"

#include <stdio.h>
#include <string.h>

char* optarg = NULL;
int optind = 1, opterr = 1, optopt = 0;

int
getopt(int argc, char* const argv[], const char* optstring) {
    static int sp = 1; /* position inside current argv element */
    const char* cp;
    int c;

    if(sp == 1) {
        if(optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;
        if(strcmp(argv[optind], "--") == 0) {
            optind++;
            return -1;
        }
    }

    optopt = c = (unsigned char)argv[optind][sp];
    if(c == ':' || (cp = strchr(optstring, c)) == NULL) {
        if(opterr && *optstring != ':')
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
        if(argv[optind][++sp] == '\0') {
            optind++;
            sp = 1;
        }
        return '?';
    }

    if(cp[1] == ':') { /* option takes an argument */
        if(argv[optind][sp + 1] != '\0') {
            optarg = &argv[optind++][sp + 1]; /* -ofile */
        } else if(++optind >= argc) {
            sp = 1;
            if(opterr && *optstring != ':')
                fprintf(stderr, "%s: option requires an argument -- %c\n",
                        argv[0], c);
            return *optstring == ':' ? ':' : '?';
        } else {
            optarg = argv[optind++]; /* -o file */
        }
        sp = 1;
    } else { /* flag option */
        if(argv[optind][++sp] == '\0') {
            sp = 1;
            optind++;
        }
        optarg = NULL;
    }
    return c;
}
