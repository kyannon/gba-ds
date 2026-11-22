/* Diagnostic timeline and system sampler: see trace.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "trace.h"

#if defined(__linux__) && !defined(__wasi__)

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

bool traceOn;
static double procStartMs; /* CLOCK_BOOTTIME (ms) when the process started */
static char phaseName[48] = "start";
static volatile double fastUntil; /* trace_ms(): the sampler runs fast until then */
static long clkTck = 100;

static double boot_ms(void) {
	struct timespec t;
	if (clock_gettime(CLOCK_BOOTTIME, &t) != 0) clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

double trace_ms(void) {
	return boot_ms() - procStartMs;
}

/* /proc/.../stat of a process or thread: the name (without its brackets) and the fields wanted */
typedef struct {
	char comm[24];
	char state;
	unsigned long utime, stime; /* clock ticks */
	long nice;
	unsigned long long start;
	int cpu;
} ProcStat;

static bool read_stat(const char* path, ProcStat* s) {
	char buf[1024];
	FILE* f = fopen(path, "r");
	if (!f) return false;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	char* open = strchr(buf, '(');
	char* close = strrchr(buf, ')');
	if (!open || !close || close < open) return false;
	size_t len = (size_t) (close - open - 1);
	if (len >= sizeof(s->comm)) len = sizeof(s->comm) - 1;
	memcpy(s->comm, open + 1, len);
	s->comm[len] = 0;
	/* fields 3 (state) to 39 (processor) of proc(5) */
	int got = sscanf(close + 2,
	                 "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %ld %*d %*d %llu "
	                 "%*u %*d %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*d %d",
	                 &s->state, &s->utime, &s->stime, &s->nice, &s->start, &s->cpu);
	return got == 6;
}

void trace_init(void) {
	clkTck = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
	ProcStat s;
	if (read_stat("/proc/self/stat", &s)) procStartMs = (double) s.start * 1000.0 / (double) clkTck;
	else procStartMs = boot_ms();
	const char* e = getenv("GBADS_TRACE");
#ifdef __ANDROID__
	traceOn = e ? atoi(e) != 0 : true;
	const char* home = getenv("GBADS_HOME");
	if (traceOn && home && *home && !e) {
		char path[1100];
		snprintf(path, sizeof(path), "%s/trace-off", home);
		if (access(path, F_OK) == 0) traceOn = false;
	}
#else
	traceOn = e && atoi(e) != 0;
#endif
}

void trace(const char* fmt, ...) {
	if (!traceOn) return;
	char buf[1000];
	int n = snprintf(buf, sizeof(buf), "[+%.1f] ", trace_ms());
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof(buf) - (size_t) n, fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s\n", buf);
}

double trace_since(const char* what, double t0) {
	double now = trace_ms();
	trace("%s: %.1f ms", what, now - t0);
	return now;
}

void trace_thread(const char* name) {
	prctl(PR_SET_NAME, name, 0, 0, 0);
}

void trace_phase(const char* name) {
	if (!traceOn) return;
	if (strcmp(phaseName, name)) {
		trace("phase: %s -> %s", phaseName, name);
		snprintf(phaseName, sizeof(phaseName), "%s", name);
	}
	fastUntil = trace_ms() + 15000.0;
}

/* ------------------------------------------------------------ stats */

#define STATS_MAX 96
static struct {
	char name[40];
	double sum, min, max;
	unsigned n;
} stats[STATS_MAX];
static int nStats;
static pthread_mutex_t statLock = PTHREAD_MUTEX_INITIALIZER;

int trace_stat(const char* name) {
	if (!traceOn) return -1;
	pthread_mutex_lock(&statLock);
	int id = -1;
	for (int i = 0; i < nStats; ++i)
		if (!strcmp(stats[i].name, name)) id = i;
	if (id < 0 && nStats < STATS_MAX) {
		id = nStats++;
		snprintf(stats[id].name, sizeof(stats[id].name), "%s", name);
	}
	pthread_mutex_unlock(&statLock);
	return id;
}

void trace_add(int id, double v) {
	if (id < 0) return;
	pthread_mutex_lock(&statLock);
	if (!stats[id].n || v < stats[id].min) stats[id].min = v;
	if (!stats[id].n || v > stats[id].max) stats[id].max = v;
	stats[id].sum += v;
	++stats[id].n;
	pthread_mutex_unlock(&statLock);
}

/* one "stats[group]" line for each group that has numbers; the numbers start over */
static void stats_report(void) {
	char groups[16][24];
	int ng = 0;
	pthread_mutex_lock(&statLock);
	for (int i = 0; i < nStats; ++i) {
		if (!stats[i].n) continue;
		char g[24];
		const char* dot = strchr(stats[i].name, '.');
		size_t len = dot ? (size_t) (dot - stats[i].name) : 0;
		if (len >= sizeof(g)) len = sizeof(g) - 1;
		memcpy(g, stats[i].name, len);
		g[len] = 0;
		bool seen = false;
		for (int k = 0; k < ng; ++k) seen |= !strcmp(groups[k], g);
		if (!seen && ng < 16) snprintf(groups[ng++], sizeof(groups[0]), "%s", g);
	}
	for (int k = 0; k < ng; ++k) {
		char line[900];
		int n = snprintf(line, sizeof(line), "stats[%s]:", groups[k]);
		size_t glen = strlen(groups[k]);
		for (int i = 0; i < nStats && n < (int) sizeof(line) - 80; ++i) {
			if (!stats[i].n || strncmp(stats[i].name, groups[k], glen) || stats[i].name[glen] != '.') continue;
			n += snprintf(line + n, sizeof(line) - (size_t) n, " %s %.1f/%.1f/%.1f x%u;", stats[i].name + glen + 1, stats[i].sum / stats[i].n,
			              stats[i].min, stats[i].max, stats[i].n);
		}
		trace("%s", line);
	}
	for (int i = 0; i < nStats; ++i) stats[i].n = 0, stats[i].sum = 0;
	pthread_mutex_unlock(&statLock);
}

/* ------------------------------------------------------------ files */

static bool read_text(const char* path, char* out, size_t size) {
	FILE* f = fopen(path, "r");
	if (!f) return false;
	size_t n = fread(out, 1, size - 1, f);
	fclose(f);
	out[n] = 0;
	while (n && (out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
	return true;
}

static long read_long(const char* path, bool* ok) {
	char b[64];
	bool got = read_text(path, b, sizeof(b)) && b[0];
	if (ok) *ok = got;
	return got ? atol(b) : 0;
}

static const char* errname(void) {
	return errno == EACCES ? "no permission" : errno == ENOENT ? "not there" : strerror(errno);
}

/* ------------------------------------------------------------ snapshot */

#define CPUS_MAX 16

static int online_cpus(void) {
	long n = sysconf(_SC_NPROCESSORS_CONF);
	return n < 1 ? 1 : n > CPUS_MAX ? CPUS_MAX : (int) n;
}

static void snapshot_props(void) {
#ifdef __ANDROID__
	static const char* const keys[] = { "ro.product.manufacturer", "ro.product.model", "ro.product.device", "ro.build.fingerprint", "ro.build.display.id",
		                                "ro.build.version.release", "ro.build.version.sdk", "ro.build.version.security_patch", "ro.build.type",
		                                "ro.hardware", "ro.board.platform", "ro.soc.model", "ro.soc.manufacturer", "ro.product.cpu.abi", "ro.config.low_ram",
		                                "ro.sf.lcd_density", "dalvik.vm.heapgrowthlimit", "dalvik.vm.heapsize", "debug.hwui.renderer",
		                                "persist.sys.performance", "ro.vendor.build.fingerprint" };
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
		char v[PROP_VALUE_MAX];
		if (__system_property_get(keys[i], v) > 0) trace("prop %s = %s", keys[i], v);
	}
#endif
}

void trace_snapshot(const char* why) {
	if (!traceOn) return;
	trace("snapshot (%s)", why);
	struct utsname u;
	if (uname(&u) == 0) trace("kernel %s %s %s", u.sysname, u.release, u.machine);
	snapshot_props();
	int ncpu = online_cpus();
	char b[256];
	for (int c = 0; c < ncpu; ++c) {
		char p[160], gov[48] = "?", avail[160] = "?";
		long cur = 0, min = 0, max = 0, hw = 0;
		bool ok;
		snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/online", c);
		bool on = !read_text(p, b, sizeof(b)) || atoi(b);
		snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", c);
		read_text(p, gov, sizeof(gov));
		snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_governors", c);
		read_text(p, avail, sizeof(avail));
		snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", c);
		cur = read_long(p, &ok);
		snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", c);
		min = read_long(p, NULL);
		snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", c);
		max = read_long(p, NULL);
		snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
		hw = read_long(p, NULL);
		trace("cpu%d: %s, governor %s (of: %s), now %s, scaling %ld-%ld MHz, hardware max %ld MHz", c, on ? "online" : "offline", gov, avail,
		      ok ? (snprintf(b, sizeof(b), "%ld MHz", cur / 1000), b) : "unreadable", min / 1000, max / 1000, hw / 1000);
	}
	/* where the system keeps its other clocks (devfreq: GPU, memory bus) */
	DIR* d = opendir("/sys/class/devfreq");
	int nd = 0;
	if (d) {
		struct dirent* e;
		while ((e = readdir(d)) && nd < 8) {
			if (e->d_name[0] == '.') continue;
			char p[300], gov[48] = "?", freqs[200] = "?";
			snprintf(p, sizeof(p), "/sys/class/devfreq/%s/governor", e->d_name);
			read_text(p, gov, sizeof(gov));
			snprintf(p, sizeof(p), "/sys/class/devfreq/%s/available_frequencies", e->d_name);
			read_text(p, freqs, sizeof(freqs));
			snprintf(p, sizeof(p), "/sys/class/devfreq/%s/cur_freq", e->d_name);
			bool ok;
			long cur = read_long(p, &ok);
			trace("devfreq %s: governor %s, now %s, available %s", e->d_name, gov, ok ? (snprintf(b, sizeof(b), "%ld MHz", cur / 1000000), b) : "unreadable", freqs);
			++nd;
		}
		closedir(d);
	} else {
		trace("devfreq: /sys/class/devfreq %s", errname());
	}
	/* which of the sampler's sources the app may read */
	static const struct {
		const char *label, *path;
	} sources[] = { { "stat", "/proc/stat" }, { "meminfo", "/proc/meminfo" }, { "loadavg", "/proc/loadavg" }, { "psi-cpu", "/proc/pressure/cpu" },
		            { "psi-memory", "/proc/pressure/memory" }, { "psi-io", "/proc/pressure/io" }, { "self-io", "/proc/self/io" },
		            { "self-status", "/proc/self/status" }, { "thermal", "/sys/class/thermal/thermal_zone0/temp" },
		            { "battery", "/sys/class/power_supply/battery/temp" }, { "cpufreq", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq" },
		            { "wchan", "/proc/self/task/1/wchan" } };
	char sline[700] = "readable:";
	for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
		FILE* f = fopen(sources[i].path, "r");
		size_t len = strlen(sline);
		snprintf(sline + len, sizeof(sline) - len, " %s=%s;", sources[i].label, f ? "yes" : errname());
		if (f) fclose(f);
	}
	trace("%s", sline);
	cpu_set_t allowed;
	if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) trace("cores this process may run on: %d of %d", CPU_COUNT(&allowed), ncpu);
	char cg[300];
	if (read_text("/proc/self/cgroup", cg, sizeof(cg))) {
		for (char* p = cg; *p; ++p)
			if (*p == '\n') *p = '|';
		trace("cgroups: %s", cg);
	}
	if (read_text("/proc/self/oom_score_adj", b, sizeof(b))) trace("oom_score_adj %s", b);
	if (read_text("/proc/meminfo", b, sizeof(b))) {
		long total = 0;
		FILE* f = fopen("/proc/meminfo", "r");
		char line[128];
		while (f && fgets(line, sizeof(line), f))
			if (sscanf(line, "MemTotal: %ld kB", &total) == 1) break;
		if (f) fclose(f);
		trace("memory total %ld MB", total / 1024);
	}
}

/* ------------------------------------------------------------ sampler */

#define THREADS_MAX 160
typedef struct {
	int tid;
	unsigned long ticks;
	char comm[24];
} Seen;
static Seen seen[THREADS_MAX];
static int nSeen;

typedef struct {
	unsigned long long user, sys_, idle, total;
} CpuTimes;

static long meminfo_kb(const char* text, const char* key) {
	const char* p = strstr(text, key);
	return p ? atol(p + strlen(key)) : -1;
}

static bool read_psi(const char* path, double* some10) {
	char b[200];
	if (!read_text(path, b, sizeof(b))) return false;
	const char* p = strstr(b, "avg10=");
	*some10 = p ? atof(p + 6) : 0;
	return true;
}

static int cmp_desc(const void* a, const void* b) {
	double x = ((const double*) a)[0], y = ((const double*) b)[0];
	return x < y ? 1 : x > y ? -1 : 0;
}

static void* sampler_main(void* arg) {
	(void) arg;
	trace_thread("gbads-trace");
	setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), 10);
	const int ncpu = online_cpus();
	CpuTimes prevCpu[CPUS_MAX + 1];
	memset(prevCpu, 0, sizeof(prevCpu));
	unsigned long long prevCtxt = 0, prevIoR = 0, prevIoW = 0;
	double prevT = trace_ms(), lastStats = prevT;
	bool first = true;
	char cgroupWas[300] = "";
	int round = 0;
	for (;;) {
		double wait = trace_ms() < fastUntil || trace_ms() < 30000.0 ? 250.0 : 1000.0;
		struct timespec ts = { 0, (long) (wait * 1e6) };
		nanosleep(&ts, NULL);
		double now = trace_ms(), dt = (now - prevT) / 1000.0;
		if (dt <= 0) continue;
		++round;

		/* /proc/stat: every core's busy share, context switches, runnable threads */
		char big[4096];
		int busy[CPUS_MAX + 1] = { 0 };
		long running = -1;
		unsigned long long ctxt = 0;
		if (read_text("/proc/stat", big, sizeof(big))) {
			for (char* line = big; line && *line;) {
				char* nl = strchr(line, '\n');
				if (nl) *nl = 0;
				if (!strncmp(line, "cpu", 3)) {
					int idx = isdigit((unsigned char) line[3]) ? atoi(line + 3) + 1 : 0;
					unsigned long long v[8] = { 0 };
					if (idx <= CPUS_MAX &&
					    sscanf(strchr(line, ' '), "%llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 4) {
						CpuTimes c = { v[0] + v[1] + v[5] + v[6], 0, v[3] + v[4], 0 };
						c.total = c.user + c.idle + v[2];
						unsigned long long dTot = c.total - prevCpu[idx].total, dIdle = c.idle - prevCpu[idx].idle;
						busy[idx] = !first && dTot ? (int) (100 * (dTot - dIdle) / dTot) : 0;
						prevCpu[idx] = c;
					}
				} else if (!strncmp(line, "ctxt ", 5)) {
					ctxt = strtoull(line + 5, NULL, 10);
				} else if (!strncmp(line, "procs_running ", 14)) {
					running = atol(line + 14);
				}
				line = nl ? nl + 1 : NULL;
			}
		}
		char mhz[160] = "", cores[160] = "";
		for (int c = 0; c < ncpu; ++c) {
			size_t a = strlen(cores), m = strlen(mhz);
			snprintf(cores + a, sizeof(cores) - a, "%s%d", c ? " " : "", busy[c + 1]);
			char p[96];
			snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", c);
			bool ok;
			long k = read_long(p, &ok);
			if (ok) snprintf(mhz + m, sizeof(mhz) - m, "%s%ld", c ? " " : "", k / 1000);
		}

		/* this process's threads: who used the CPU since last time */
		struct {
			double pct;
			char text[160];
		} rows[THREADS_MAX];
		int nrows = 0;
		Seen now_seen[THREADS_MAX];
		int nNow = 0;
		double procPct = 0;
		DIR* d = opendir("/proc/self/task");
		if (d) {
			struct dirent* e;
			while ((e = readdir(d)) && nNow < THREADS_MAX) {
				if (e->d_name[0] == '.') continue;
				int tid = atoi(e->d_name);
				char p[96];
				snprintf(p, sizeof(p), "/proc/self/task/%d/stat", tid);
				ProcStat s;
				if (!read_stat(p, &s)) continue;
				unsigned long ticks = s.utime + s.stime, was = 0;
				bool known = false;
				for (int i = 0; i < nSeen; ++i)
					if (seen[i].tid == tid) was = seen[i].ticks, known = true;
				now_seen[nNow++] = (Seen) { tid, ticks, "" };
				double pct = known ? 100.0 * (double) (ticks - was) / ((double) clkTck * dt) : 0;
				procPct += pct;
				if (pct >= 2.0 || s.state == 'D') {
					char wch[80] = "";
					if (s.state == 'D') {
						char w[96], t[40];
						snprintf(w, sizeof(w), "/proc/self/task/%d/wchan", tid);
						if (read_text(w, t, sizeof(t)) && t[0] && strcmp(t, "0")) snprintf(wch, sizeof(wch), " waiting in %s", t);
					}
					if (nrows < THREADS_MAX) {
						rows[nrows].pct = pct;
						snprintf(rows[nrows].text, sizeof(rows[nrows].text), "%s/%d %.0f%% on cpu%d%s%s", s.comm, tid, pct, s.cpu, s.state == 'D' ? " IN I/O WAIT" : "", wch);
						++nrows;
					}
				}
			}
			closedir(d);
		}
		memcpy(seen, now_seen, sizeof(Seen) * (size_t) nNow);
		nSeen = nNow;

		/* memory, I/O, pressure, battery, temperatures */
		char mem[4096], st[2048];
		long avail = -1, rss = -1, hwm = -1, swap = -1;
		if (read_text("/proc/meminfo", mem, sizeof(mem))) avail = meminfo_kb(mem, "MemAvailable:") / 1024;
		if (read_text("/proc/self/status", st, sizeof(st))) {
			rss = meminfo_kb(st, "VmRSS:") / 1024;
			hwm = meminfo_kb(st, "VmHWM:") / 1024;
			swap = meminfo_kb(st, "VmSwap:") / 1024;
		}
		char io[1024];
		unsigned long long ior = 0, iow = 0;
		bool haveIo = false;
		if (read_text("/proc/self/io", io, sizeof(io))) {
			const char* p = strstr(io, "read_bytes: ");
			const char* q = strstr(io, "write_bytes: ");
			if (p && q) ior = strtoull(p + 12, NULL, 10), iow = strtoull(q + 13, NULL, 10), haveIo = true;
		}
		double pc = 0, pm = 0, pi = 0;
		bool psi = read_psi("/proc/pressure/cpu", &pc);
		read_psi("/proc/pressure/memory", &pm);
		read_psi("/proc/pressure/io", &pi);
		char load[64] = "";
		read_text("/proc/loadavg", load, sizeof(load));
		char* sp = strchr(load, ' ');
		if (sp && (sp = strchr(sp + 1, ' ')) && (sp = strchr(sp + 1, ' '))) *sp = 0; /* the three averages */

		char temps[300] = "";
		for (int z = 0; z < 12; ++z) {
			char p[96], type[32] = "?", v[24];
			snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/temp", z);
			if (!read_text(p, v, sizeof(v))) continue;
			snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/type", z);
			read_text(p, type, sizeof(type));
			long t = atol(v);
			size_t len = strlen(temps);
			if (len < sizeof(temps) - 40) snprintf(temps + len, sizeof(temps) - len, " %s %.0f", type, t > 1000 ? t / 1000.0 : (double) t);
		}
		char gpu[200] = "";
		if (round % 2 == 0 || first) {
			DIR* dv = opendir("/sys/class/devfreq");
			if (dv) {
				struct dirent* e;
				while ((e = readdir(dv))) {
					if (e->d_name[0] == '.') continue;
					char p[300], ld[48] = "";
					bool ok;
					snprintf(p, sizeof(p), "/sys/class/devfreq/%s/cur_freq", e->d_name);
					long k = read_long(p, &ok);
					if (!ok) continue;
					snprintf(p, sizeof(p), "/sys/class/devfreq/%s/load", e->d_name);
					read_text(p, ld, sizeof(ld));
					size_t len = strlen(gpu);
					if (len < sizeof(gpu) - 60) snprintf(gpu + len, sizeof(gpu) - len, " %.60s %ld MHz%s%s;", e->d_name, k / 1000000, ld[0] ? " load " : "", ld);
				}
				closedir(dv);
			}
		}
		char batt[100] = "";
		if (round % 4 == 0 || first) {
			bool ok1, ok2;
			long t = read_long("/sys/class/power_supply/battery/temp", &ok1);
			long cap = read_long("/sys/class/power_supply/battery/capacity", &ok2);
			if (ok1 || ok2) snprintf(batt, sizeof(batt), " | battery %ld%% %.1fC", cap, t / 10.0);
		}

		/* the sampler line, and the threads */
		trace("sys: %s cpu%% [%s] total %d%% MHz [%s] runnable %ld ctx %.0f/s load %s | proc %.0f%% | mem avail %ld MB, rss %ld (peak %ld, swap %ld) MB | io read %.0f write %.0f KB/s%s%s%s%s%s",
		      phaseName, cores, busy[0], mhz, running, !first && ctxt >= prevCtxt ? (double) (ctxt - prevCtxt) / dt : 0.0, load, procPct, avail, rss, hwm, swap,
		      haveIo && !first ? (double) (ior - prevIoR) / 1024.0 / dt : 0.0, haveIo && !first ? (double) (iow - prevIoW) / 1024.0 / dt : 0.0,
		      psi ? " | pressure(avg10) cpu " : "", psi ? (snprintf(big, sizeof(big), "%.2f mem %.2f io %.2f", pc, pm, pi), big) : "",
		      temps[0] ? " | temps" : "", temps, batt);
		if (gpu[0]) trace("devfreq:%s", gpu);
		if (nrows) {
			qsort(rows, (size_t) nrows, sizeof(rows[0]), cmp_desc);
			char line[900];
			int n = snprintf(line, sizeof(line), "threads:");
			for (int i = 0; i < nrows && i < 12 && n < (int) sizeof(line) - 100; ++i) n += snprintf(line + n, sizeof(line) - (size_t) n, " %s;", rows[i].text);
			trace("%s", line);
		}
		char cg[300];
		if (read_text("/proc/self/cgroup", cg, sizeof(cg)) && strcmp(cg, cgroupWas)) {
			snprintf(cgroupWas, sizeof(cgroupWas), "%s", cg);
			for (char* p = cg; *p; ++p)
				if (*p == '\n') *p = '|';
			trace("cgroups now: %s", cg);
		}
		prevCtxt = ctxt, prevIoR = ior, prevIoW = iow;
		prevT = now;
		first = false;
		if (now - lastStats >= 990.0) {
			stats_report();
			lastStats = now;
		}
	}
	return NULL;
}

void trace_start(void) {
	if (!traceOn) return;
	static bool started;
	if (started) return;
	started = true;
	trace_snapshot("at start");
	pthread_t t;
	if (pthread_create(&t, NULL, sampler_main, NULL) == 0) pthread_detach(t);
}

#endif
