/* win32/libgen.h -- minimal POSIX basename()/dirname() for MSVC */
#ifndef WIN32_LIBGEN_H
#define WIN32_LIBGEN_H

#include <string.h>

static int libgen_is_sep_(char c) { return c == '/' || c == '\\'; }

/* Like POSIX, these may modify the argument and return a pointer into it
   or into a static buffer. Not thread-safe. */

static char *basename(char *path) {
    static char dot[] = ".";
    char *end, *start;
    if (!path || !*path) return dot;
    if (path[0] && path[1] == ':') path += 2;            /* skip "C:" */
    end = path + strlen(path);
    while (end > path && libgen_is_sep_(end[-1])) end--;  /* strip trailing seps */
    if (end == path) {                                    /* all separators, or empty */
        static char root[] = "/";
        return *path ? root : dot;
    }
    *end = '\0';
    start = end;
    while (start > path && !libgen_is_sep_(start[-1])) start--;
    return start;
}

static char *dirname(char *path) {
    static char dot[] = ".";
    char *p, *base = path;
    if (!path || !*path) return dot;
    if (path[0] && path[1] == ':') base = path + 2;       /* keep "C:" prefix */
    p = base + strlen(base);
    while (p > base && libgen_is_sep_(p[-1])) p--;        /* trailing seps */
    while (p > base && !libgen_is_sep_(p[-1])) p--;       /* last component */
    while (p > base + 1 && libgen_is_sep_(p[-1])) p--;    /* seps before it */
    if (p == base) {
        if (base > path || libgen_is_sep_(*base)) {       /* "C:", "C:\x", "\x" */
            if (libgen_is_sep_(*base)) base++;
            *base = '\0';
            return path;
        }
        return dot;
    }
    *p = '\0';
    return path;
}

#endif