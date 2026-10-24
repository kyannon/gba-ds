/* Updates (Linux): asks GitHub for the latest stable release, and on a yes
 * downloads its Linux zip, checks it, installs it over the app's folder and
 * restarts. Android and the web build get no-op stubs. */
#ifndef GBADS_UPDATE_H
#define GBADS_UPDATE_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	UPD_IDLE,        /* nothing asked yet this run */
	UPD_CHECKING,
	UPD_LATEST,      /* this is the newest release */
	UPD_AVAILABLE,   /* a newer one: version, notes */
	UPD_DOWNLOADING, /* progress */
	UPD_INSTALLING,
	UPD_DONE,        /* installed: restart */
	UPD_ERROR        /* error */
} UpdateState;

typedef struct {
	UpdateState state;
	char version[32];  /* the newer release's, without the v */
	char notes[1200];  /* its "What's new", as plain text */
	char error[200];
	int percent;       /* download progress */
	long size;         /* the download's, in bytes */
	long lastCheck;    /* time of the last successful check (0: never) */
	bool autoOn;
} UpdateInfo;

#if !defined(__ANDROID__) && !defined(__wasi__)
#define GBADS_UPDATES 1
/* exeDir: the app's folder; argv: for the restart */
void update_init(const Config* cfg, const char* exeDir, int argc, char** argv);
/* The game list opened: check in the background if automatic checks are on
 * and the last one was a day ago or more (once per run at most). */
void update_auto_check(void);
void update_check_now(void);
void update_install(void);  /* from UPD_AVAILABLE */
void update_cancel(void);   /* a download */
void update_skip(void);     /* don't offer this version again */
void update_set_auto(bool on);
void update_info(UpdateInfo* out);
/* An automatic check found a release the player hasn't answered yet: show
 * the notice. update_notice_seen() once it's shown. */
bool update_notice_pending(void);
void update_notice_seen(void);
/* After everything is shut down: start the new version (returns only if
 * it couldn't). */
void update_restart_if_installed(void);
#else
static inline void update_init(const Config* cfg, const char* exeDir, int argc, char** argv) { (void) cfg, (void) exeDir, (void) argc, (void) argv; }
static inline void update_auto_check(void) {}
static inline bool update_notice_pending(void) { return false; }
static inline void update_notice_seen(void) {}
static inline void update_restart_if_installed(void) {}
#endif

#endif
