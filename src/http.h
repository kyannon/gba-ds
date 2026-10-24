/* HTTP GETs for the updater (Linux): the curl program, or libcurl loaded at
 * run time where there's no program (BananaOS). Blocking: call from a
 * worker thread. */
#ifndef GBADS_HTTP_H
#define GBADS_HTTP_H

#include <stdbool.h>
#include <stddef.h>

/* GET url. With outPath, the body goes to outPath (written as
 * outPath.part, renamed when complete); else to *body (malloc'd, NUL
 * terminated) and *len. Returns the HTTP status (200...), or -1 if the
 * request failed; *err (if given) says why. *cancel, if set during a
 * download, stops it (libcurl) or is checked after (curl). */
int http_get(const char* url, const char* userAgent, int timeoutSec, const char* outPath, char** body, size_t* len,
             char* err, size_t errSize, volatile bool* cancel);

#endif
