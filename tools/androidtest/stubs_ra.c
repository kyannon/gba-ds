/* desktop stand-ins for android.c's RetroAchievements parts: HTTP through
 * the curl command, a "dialog" that answers with RA_USER / RA_PASS */
#include "android.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int android_http(const char* url, const char* ctype, const char* post, const char* ua, const char* outPath, char** body, size_t* len) {
	if (body) *body = NULL;
	if (len) *len = 0;
	char postFile[] = "/tmp/gbads-rapostXXXXXX", cmd[4096];
	if (post) {
		int fd = mkstemp(postFile);
		if (write(fd, post, strlen(post)) < 0) {}
		close(fd);
	}
	if (outPath) {
		snprintf(cmd, sizeof cmd, "curl -sS -f -L -A '%s' -o '%s' '%s' -w '%%{http_code}'", ua, outPath, url);
	} else {
		snprintf(cmd, sizeof cmd, "curl -sS -A '%s' %s%s%s%s%s -w '\\n%%{http_code}' '%s'", ua, post ? "-H 'Content-Type: " : "", post ? ctype : "",
		         post ? "' --data-binary @" : "", post ? postFile : "", "", url);
	}
	FILE* p = popen(cmd, "r");
	size_t cap = 1 << 16, n = 0;
	char* buf = malloc(cap + 1);
	size_t r;
	while (p && (r = fread(buf + n, 1, cap - n, p)) > 0) n += r;
	if (p) pclose(p);
	if (post) unlink(postFile);
	buf[n] = 0;
	int code = -1;
	char* nl = strrchr(buf, '\n');
	if (outPath) code = atoi(buf);
	else if (nl) code = atoi(nl + 1), *nl = 0, n = (size_t) (nl - buf);
	if (body && !outPath) *body = buf, *len = n;
	else free(buf);
	return code > 0 ? code : -1;
}
static int asked;
bool android_ra_dialog(const char* user) { (void) user; asked = 1; return true; }
int android_take_ra_credentials(char* u, size_t un, char* p, size_t pn) {
	if (!asked) return 0;
	asked = 0;
	snprintf(u, un, "%s", getenv("RA_USER"));
	snprintf(p, pn, "%s", getenv("RA_PASS"));
	return 1;
}
