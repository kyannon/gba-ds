package io.github.kyannon.gbads;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.app.usage.UsageStatsManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.display.DisplayManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Diagnostics for GammaOS performance (issue #36), the Java half of the
 * native trace (src/trace.h). Everything is stamped "[+N]", the milliseconds
 * since this process started, the same clock as the native log's, so
 * launch.log and gba-ds.log line up. Lines go to launch.log (and logcat).
 *
 * <ul>
 * <li>why the previous run of the app ended (Android's own record: a crash,
 * low memory, the system killing it, a normal exit, and when);
 * <li>device, memory, display, power and thermal facts at start;
 * <li>the lifecycle (activities, the second screen's surface, trimming,
 * finish, exit), each with its time;
 * <li>a watchdog that says when the UI thread didn't answer for a while;
 * <li>memory and thermal changes;
 * <li>the app's own logcat (the system's messages about this process:
 * buffers, frame skips, GC) into logcat.log, the last run's kept as
 * logcat.prev.log.
 * </ul>
 * Off when GBA DS/trace-off exists.
 */
final class Trace {
    private static final String TAG = "GBADS";
    private static boolean started;
    private static Boolean enabled;
    private static Context appContext;
    private static java.lang.Process logcatProcess;

    private Trace() {
    }

    /** Milliseconds since this process was started. */
    static long ms() {
        return SystemClock.elapsedRealtime() - Process.getStartElapsedRealtime();
    }

    static boolean on(Context c) {
        if (enabled == null) {
            try {
                enabled = !new File(LauncherActivity.dataDir(c), "trace-off").exists();
            } catch (RuntimeException e) {
                enabled = true;
            }
        }
        return enabled;
    }

    /** One line in launch.log, stamped. */
    static void log(Context c, String what) {
        if (!on(c)) {
            Log.i(TAG, what);
            return;
        }
        LauncherActivity.launchLog(c, "[+" + ms() + "] " + what);
    }

    /** What happened since t0 (a ms() from before), as a line. Returns now. */
    static long since(Context c, String what, long t0) {
        long now = ms();
        log(c, what + ": " + (now - t0) + " ms");
        return now;
    }

    /** Once per process: the facts, the watchdog, the logcat copy. */
    static synchronized void start(Context c, String who) {
        if (started) return;
        started = true;
        appContext = c.getApplicationContext();
        if (!on(c)) return;
        log(c, who + ": the first of our Java code ran, the process is " + ms() + " ms old (pid " + Process.myPid() + ", uid " + Process.myUid() + ")");
        /* the rest off the UI thread: reading these costs tens of milliseconds, and the start-up is what is being timed */
        final Context app = appContext;
        Thread t = new Thread(() -> {
            startLogcat(app);
            try {
                previousExits(app);
                facts(app);
                watchThermal(app);
            } catch (RuntimeException e) {
                log(app, "trace: couldn't read some facts: " + e);
            }
            startWatchdog(app);
        }, "gbads-trace-start");
        t.setPriority(Thread.MIN_PRIORITY);
        t.start();
        Runtime.getRuntime().addShutdownHook(new Thread(() -> LauncherActivity.launchLog(appContext, "[+" + ms() + "] the Java VM is shutting down (exit hooks running)"), "gbads-exit"));
    }

    /* ------------------------------------------------------------ why the last run ended */

    private static String reasonName(int r) {
        switch (r) {
            case 1: return "EXIT_SELF (the app called exit: a normal close)";
            case 2: return "SIGNALED (killed by a signal)";
            case 3: return "LOW_MEMORY (the system killed it for memory)";
            case 4: return "CRASH (Java exception)";
            case 5: return "CRASH_NATIVE (a native crash)";
            case 6: return "ANR (not responding)";
            case 7: return "INITIALIZATION_FAILURE";
            case 8: return "PERMISSION_CHANGE";
            case 9: return "EXCESSIVE_RESOURCE_USAGE";
            case 10: return "USER_REQUESTED (swiped away or force-stopped)";
            case 11: return "USER_STOPPED";
            case 12: return "DEPENDENCY_DIED";
            case 13: return "OTHER";
            case 14: return "FREEZER (frozen then killed)";
            case 15: return "PACKAGE_STATE_CHANGE";
            case 16: return "PACKAGE_UPDATED (replaced by an install)";
            default: return "UNKNOWN (" + r + ")";
        }
    }

    private static void previousExits(Context c) {
        if (Build.VERSION.SDK_INT < 30) {
            log(c, "previous exits: Android " + Build.VERSION.SDK_INT + " doesn't keep a record (needs 11)");
            return;
        }
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(null, 0, 6);
        SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US);
        long nowWall = System.currentTimeMillis();
        log(c, "previous exits (newest first; Android's record of how this app's earlier processes ended): " + list.size());
        for (ApplicationExitInfo e : list) {
            log(c, "  ended " + f.format(new Date(e.getTimestamp())) + " (" + ((nowWall - e.getTimestamp()) / 1000) + " s ago), pid " + e.getPid()
                    + ", " + reasonName(e.getReason()) + ", status " + e.getStatus() + ", importance " + e.getImportance()
                    + ", pss " + (e.getPss() / 1024) + " MB, rss " + (e.getRss() / 1024) + " MB"
                    + (e.getDescription() != null ? ", \"" + e.getDescription() + "\"" : ""));
        }
    }

    /* ------------------------------------------------------------ facts at start */

    private static void facts(Context c) {
        log(c, "device: " + Build.MANUFACTURER + " " + Build.MODEL + " (" + Build.DEVICE + ", " + Build.HARDWARE + "/" + Build.BOARD + "), Android "
                + Build.VERSION.RELEASE + " API " + Build.VERSION.SDK_INT + ", build " + Build.DISPLAY + " [" + Build.VERSION.INCREMENTAL + "] " + Build.TAGS + "/" + Build.TYPE);
        log(c, "fingerprint: " + Build.FINGERPRINT);
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        log(c, "memory: total " + (mi.totalMem >> 20) + " MB, available " + (mi.availMem >> 20) + " MB, low-memory threshold " + (mi.threshold >> 20) + " MB, "
                + (mi.lowMemory ? "LOW MEMORY NOW" : "not low") + "; app heap limit " + am.getMemoryClass() + " MB (large " + am.getLargeMemoryClass() + " MB), low-RAM device: " + am.isLowRamDevice());
        PowerManager pm = (PowerManager) c.getSystemService(Context.POWER_SERVICE);
        StringBuilder p = new StringBuilder("power: save mode " + pm.isPowerSaveMode() + ", interactive " + pm.isInteractive()
                + ", sustained-performance mode supported " + pm.isSustainedPerformanceModeSupported()
                + ", ignoring battery optimisation " + pm.isIgnoringBatteryOptimizations(c.getPackageName()));
        if (Build.VERSION.SDK_INT >= 29) p.append(", thermal status ").append(thermalName(pm.getCurrentThermalStatus()));
        if (Build.VERSION.SDK_INT >= 28) {
            p.append(", background-restricted ").append(am.isBackgroundRestricted());
            try {
                UsageStatsManager us = (UsageStatsManager) c.getSystemService(Context.USAGE_STATS_SERVICE);
                if (us != null) p.append(", standby bucket ").append(us.getAppStandbyBucket()).append(" (10 active, 20 working set, 30 frequent, 40 rare, 45 restricted)");
            } catch (RuntimeException e) {
                p.append(", standby bucket unreadable");
            }
        }
        log(c, p.toString());
        Intent b = c.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (b != null) {
            log(c, "battery: " + b.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) + "%, " + (b.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) / 10.0) + " C, plugged "
                    + b.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) + ", status " + b.getIntExtra(BatteryManager.EXTRA_STATUS, 0));
        }
        DisplayManager dm = (DisplayManager) c.getSystemService(Context.DISPLAY_SERVICE);
        for (Display d : dm.getDisplays()) displayFacts(c, d);
        log(c, "native heap " + (Debug.getNativeHeapAllocatedSize() >> 20) + " MB, java heap " + ((Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory()) >> 20) + " MB");
    }

    /** A display's size, refresh rates and modes (a 60 Hz game on a display set to something else shows in this). */
    static void displayFacts(Context c, Display d) {
        StringBuilder b = new StringBuilder("display " + d.getDisplayId() + " \"" + d.getName() + "\": " + d.getRefreshRate() + " Hz now");
        Display.Mode cur = d.getMode();
        b.append(", mode ").append(cur.getPhysicalWidth()).append("x").append(cur.getPhysicalHeight()).append("@").append(cur.getRefreshRate());
        b.append("; modes:");
        for (Display.Mode m : d.getSupportedModes()) b.append(' ').append(m.getPhysicalWidth()).append("x").append(m.getPhysicalHeight()).append("@").append(m.getRefreshRate());
        log(c, b.toString());
    }

    /* ------------------------------------------------------------ thermal and memory */

    private static String thermalName(int s) {
        switch (s) {
            case 0: return "NONE";
            case 1: return "LIGHT";
            case 2: return "MODERATE";
            case 3: return "SEVERE";
            case 4: return "CRITICAL";
            case 5: return "EMERGENCY";
            case 6: return "SHUTDOWN";
            default: return "?" + s;
        }
    }

    private static void watchThermal(Context c) {
        if (Build.VERSION.SDK_INT < 29) return;
        PowerManager pm = (PowerManager) c.getSystemService(Context.POWER_SERVICE);
        pm.addThermalStatusListener(status -> log(appContext, "THERMAL STATUS CHANGED: " + thermalName(status)));
    }

    /** Called by the activities' onTrimMemory. */
    static void trim(Context c, int level) {
        String n = level >= 80 ? "COMPLETE" : level >= 60 ? "MODERATE" : level >= 40 ? "BACKGROUND" : level >= 20 ? "UI_HIDDEN"
                : level >= 15 ? "RUNNING_CRITICAL" : level >= 10 ? "RUNNING_LOW" : level >= 5 ? "RUNNING_MODERATE" : "?";
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        log(c, "onTrimMemory(" + level + " " + n + "): the system is short of memory; available " + (mi.availMem >> 20) + " MB, threshold " + (mi.threshold >> 20) + " MB");
    }

    /* ------------------------------------------------------------ watchdog */

    /**
     * Posts to the UI thread twice a second and says when it took long to
     * answer (the UI thread busy or blocked: another sign of a slow system,
     * or of a lock held by the game thread), and every 5 s the memory
     * numbers, faster during the first 30 s.
     */
    private static void startWatchdog(Context c) {
        final Handler main = new Handler(Looper.getMainLooper());
        final ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        Thread t = new Thread(() -> {
            long blockedSince = 0;
            long lastMem = 0;
            for (;;) {
                final long[] ran = { -1 };
                final long posted = SystemClock.uptimeMillis();
                main.post(() -> ran[0] = SystemClock.uptimeMillis());
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    return;
                }
                long now = SystemClock.uptimeMillis();
                if (ran[0] < 0) {
                    if (blockedSince == 0) blockedSince = posted;
                    if ((now - blockedSince) % 2000 < 500) log(appContext, "UI THREAD NOT ANSWERING for " + (now - blockedSince) + " ms so far");
                } else {
                    long late = ran[0] - posted;
                    if (blockedSince != 0) {
                        log(appContext, "the UI thread answered again after " + (ran[0] - blockedSince) + " ms");
                        blockedSince = 0;
                    } else if (late > 100) {
                        log(appContext, "the UI thread was slow to answer: " + late + " ms");
                    }
                }
                long upNow = ms();
                if (upNow - lastMem >= (upNow < 30000 ? 2000 : 5000)) {
                    lastMem = upNow;
                    ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
                    am.getMemoryInfo(mi);
                    Runtime rt = Runtime.getRuntime();
                    log(appContext, "java: memory available " + (mi.availMem >> 20) + " MB" + (mi.lowMemory ? " (LOW)" : "") + ", java heap "
                            + ((rt.totalMemory() - rt.freeMemory()) >> 20) + "/" + (rt.maxMemory() >> 20) + " MB, native heap " + (Debug.getNativeHeapAllocatedSize() >> 20) + " MB, threads "
                            + Thread.activeCount());
                }
            }
        }, "gbads-watchdog");
        t.setDaemon(true);
        t.setPriority(Thread.MIN_PRIORITY);
        t.start();
    }

    /* ------------------------------------------------------------ logcat */

    /**
     * The app's own logcat (what the system and its libraries say about this
     * process: BufferQueue, Choreographer's "skipped frames", GC, EGL...) into
     * logcat.log in the GBA DS folder, so it can be read without adb. The
     * last run's file is kept as logcat.prev.log. At most 8 MB a run.
     */
    private static void startLogcat(Context c) {
        final File dir = LauncherActivity.dataDir(c);
        final File out = new File(dir, "logcat.log"), prev = new File(dir, "logcat.prev.log");
        try {
            if (out.exists()) {
                prev.delete();
                out.renameTo(prev);
            }
            logcatProcess = new ProcessBuilder("logcat", "-b", "all", "-v", "threadtime", "--pid=" + Process.myPid(), "*:I")
                    .redirectErrorStream(true).start();
        } catch (Exception e) {
            log(c, "logcat: couldn't start (" + e + "): the system's messages about this app won't be in logcat.log");
            return;
        }
        final InputStream in = new BufferedInputStream(logcatProcess.getInputStream());
        Thread t = new Thread(() -> {
            byte[] buf = new byte[16384];
            long total = 0;
            try (OutputStream os = new FileOutputStream(out)) {
                for (int n; (n = in.read(buf)) > 0; ) {
                    os.write(buf, 0, n);
                    os.flush();
                    total += n;
                    if (total > 8L << 20) {
                        logcatProcess.destroy();
                        os.write("\n[logcat.log stopped at 8 MB]\n".getBytes("UTF-8"));
                        break;
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "logcat copy: " + e);
            }
            try {
                int code = logcatProcess.waitFor();
                log(appContext, "logcat: the copy ended (exit " + code + ", " + total + " bytes)" + (total == 0 ? ": nothing came, so this system may not let the app read its own log" : ""));
            } catch (InterruptedException ignored) {
            }
        }, "gbads-logcat");
        t.setDaemon(true);
        t.setPriority(Thread.MIN_PRIORITY);
        t.start();
        log(c, "logcat: copying this app's own system log to " + out);
    }

    /** The reason a java stack is interesting: who asked for this. */
    static String caller() {
        StringBuilder b = new StringBuilder();
        StackTraceElement[] st = new Throwable().getStackTrace();
        for (int i = 2; i < st.length && i < 9; ++i) b.append(i > 2 ? " < " : "").append(st[i].getClassName().replaceFirst(".*\\.", "")).append('.').append(st[i].getMethodName());
        return b.toString();
    }
}
