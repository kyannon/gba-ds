package io.github.kyannon.gbads;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.view.Display;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The app icon, and what frontends start. Asks for "All files access" once
 * (so ROMs can be read from any folder), then starts the game on the main
 * display, where SDL draws; the game activity puts its second screen on the
 * other display.
 *
 * A game to play straight away (skipping the game list) can come as
 * - an intent extra "rom" (also "ROM", "path", "file"...) with a file path, or
 *   a file:// / content:// URI, e.g. ES-DE: %EXTRA_rom%=%ROM%
 * - the intent's data, e.g. ES-DE: %ACTION%=android.intent.action.VIEW %DATA%=%ROMSAF%,
 *   or Daijisho: -a android.intent.action.VIEW -d {file.uri}
 */
public class LauncherActivity extends Activity {
    private static final String TAG = "GBADS";
    private boolean askedForFiles;
    private boolean starting; /* the game is being started (onResume can come twice) */
    private static boolean crashLogInstalled;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) askedForFiles = state.getBoolean("askedForFiles");
        installCrashLog(getApplicationContext());
    }

    /* ------------------------------------------------------------ launch.log
     * What frontends sent and what came of it, in the GBA DS folder, so a
     * launch that fails before the game starts (when gba-ds.log isn't
     * written) can be looked at without adb. */

    static File dataDir(Context c) {
        if (Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()) {
            File shared = new File(Environment.getExternalStorageDirectory(), "GBA DS");
            if (shared.isDirectory() || shared.mkdirs()) return shared;
        }
        File app = c.getExternalFilesDir(null);
        return app != null ? app : c.getFilesDir();
    }

    static synchronized void launchLog(Context c, String line) {
        Log.i(TAG, line);
        try {
            File f = new File(dataDir(c), "launch.log");
            if (f.length() > 64 * 1024) f.delete(); /* keep it short */
            try (FileOutputStream os = new FileOutputStream(f, true)) {
                String stamp = new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(new java.util.Date());
                os.write((stamp + "  " + line + "\n").getBytes("UTF-8"));
            }
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "launch.log: " + e);
        }
    }

    /** Java crashes (in the launcher or the game's Java side) go to launch.log too. */
    private static synchronized void installCrashLog(Context app) {
        if (crashLogInstalled) return;
        crashLogInstalled = true;
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, e) -> {
            java.io.StringWriter sw = new java.io.StringWriter();
            e.printStackTrace(new java.io.PrintWriter(sw));
            launchLog(app, "crashed (" + thread.getName() + "): " + sw);
            if (previous != null) previous.uncaughtException(thread, e);
        });
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putBoolean("askedForFiles", askedForFiles);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
            if (!askedForFiles && !getPreferences(MODE_PRIVATE).getBoolean("filesNotNow", false)) {
                askedForFiles = true;
                askForFiles(); /* the game starts from the dialog, or when coming back from the settings */
                return;
            } else {
                Log.w(TAG, "all files access not granted; ROM folders may be unreadable");
            }
        }
        startGame();
    }

    /** Says what All files access is for before Android's settings page opens
     * (that page alone doesn't say why), then opens it; "Not now" goes on
     * without it (files then stay in the app's own folder). */
    private void askForFiles() {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(R.string.files_title)
                .setMessage(R.string.files_why)
                .setCancelable(false)
                .setPositiveButton(R.string.files_open, (d, w) -> {
                    if (!openFilesSettings()) startGame();
                })
                .setNegativeButton(R.string.files_not_now, (d, w) -> {
                    /* not asked again at every start (Android's settings can still allow it) */
                    getPreferences(MODE_PRIVATE).edit().putBoolean("filesNotNow", true).apply();
                    startGame();
                })
                .show();
    }

    private boolean openFilesSettings() {
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
            return true;
        } catch (ActivityNotFoundException e) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                return true;
            } catch (ActivityNotFoundException e2) {
                Log.w(TAG, "no settings screen for all files access");
                Toast.makeText(this, R.string.need_files, Toast.LENGTH_LONG).show();
                return false;
            }
        }
    }

    private void startGame() {
        if (starting) return;
        starting = true;
        final Intent in = getIntent();
        final Context app = getApplicationContext();
        /* off the UI thread: a game from another app's provider may have to be
         * copied, which takes a while for a big ROM */
        new Thread(() -> {
            String rom = null;
            try {
                rom = romFromIntent(app, in);
            } catch (Throwable t) {
                launchLog(app, "couldn't get the game from the launch request: " + t);
            }
            final String found = rom;
            runOnUiThread(() -> launchGame(found));
        }, "gbads-launch").start();
    }

    private void launchGame(String rom) {
        Intent game = new Intent(this, GbaDsActivity.class);
        if (rom != null) {
            launchLog(this, "game from the launch request: " + rom);
            game.putExtra("rom", rom);
        }
        /* A task of its own (see taskAffinity in the manifest): started inside
         * this launcher's task, it would open on whatever display the launcher
         * was put on (GammaOS: often the bottom one), ignoring the display asked for. */
        game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Log.i(TAG, "launcher on display " + getWindowManager().getDefaultDisplay().getDisplayId()
                + ", starting the game on display " + Display.DEFAULT_DISPLAY);
        ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(Display.DEFAULT_DISPLAY);
        try {
            startActivity(game, options.toBundle());
        } catch (SecurityException | IllegalArgumentException e) {
            Log.w(TAG, "could not pick the display: " + e);
            try {
                startActivity(game);
            } catch (RuntimeException e2) {
                launchLog(this, "couldn't start the game: " + e2);
            }
        }
        finish();
    }

    /* ------------------------------------------------------------ the ROM from an intent */

    /* the names frontends' templates use for the game (GammaOS's examples: bootPath) */
    private static final String[] ROM_EXTRAS = { "rom", "ROM", "Rom", "path", "PATH", "file", "FILE", "romPath", "GamePath",
            "bootPath", "BootPath", "uri", "URI", "romUri", "filePath", "file_path", "rom_path", "gamePath", "game" };

    /** Everything a launch request carries, for the log (frontends differ; this shows what one sent). */
    private static void logIntent(Context c, Intent in) {
        StringBuilder b = new StringBuilder("launched with action=").append(in.getAction())
                .append(" data=").append(in.getData()).append(" type=").append(in.getType())
                .append(" flags=0x").append(Integer.toHexString(in.getFlags()));
        if (in.getComponent() != null) b.append(" component=").append(in.getComponent().flattenToShortString());
        if (in.getCategories() != null) b.append(" categories=").append(in.getCategories());
        try {
            Bundle ex = in.getExtras();
            if (ex != null) {
                for (String k : ex.keySet()) {
                    Object v;
                    try {
                        v = ex.get(k);
                    } catch (RuntimeException e) {
                        v = "(unreadable: " + e.getClass().getSimpleName() + ")";
                    }
                    b.append(" extra ").append(k).append('=').append(v);
                }
            }
        } catch (RuntimeException e) {
            /* extras of a type only the sender knows: reading them throws */
            b.append(" extras unreadable (").append(e).append(')');
        }
        if (in.getClipData() != null && in.getClipData().getItemCount() > 0) b.append(" clip=").append(in.getClipData().getItemAt(0).getUri());
        launchLog(c, b.toString());
    }

    /** One extra, or null if it's missing or can't be read. */
    private static Object extra(Intent in, String k) {
        try {
            Bundle ex = in.getExtras();
            return ex != null ? ex.get(k) : null;
        } catch (RuntimeException e) {
            return null;
        }
    }

    static String romFromIntent(Context c, Intent in) {
        if (in == null) return null;
        logIntent(c, in);
        for (String k : ROM_EXTRAS) {
            Object v = extra(in, k);
            if (v instanceof Uri) return fromUri(c, (Uri) v);
            if (v instanceof String && !((String) v).isEmpty()) {
                String str = (String) v;
                return str.startsWith("file:") || str.startsWith("content:") ? fromUri(c, Uri.parse(str)) : str;
            }
        }
        if (in.getData() != null) return fromUri(c, in.getData());
        /* some send the file as ClipData only */
        if (in.getClipData() != null && in.getClipData().getItemCount() > 0 && in.getClipData().getItemAt(0).getUri() != null) {
            return fromUri(c, in.getClipData().getItemAt(0).getUri());
        }
        return null;
    }

    private static String fromUri(Context c, Uri u) {
        if ("file".equals(u.getScheme())) return u.getPath();
        /* a bare path (some frontends: data=/storage/.../game.gba) */
        if (u.getScheme() == null && u.getPath() != null && u.getPath().startsWith("/")) return u.getPath();
        if (!"content".equals(u.getScheme())) return null;
        if ("com.android.externalstorage.documents".equals(u.getAuthority())) {
            /* with All files access, the plain path is readable (and saves can go next to the ROM) */
            String id = null;
            try {
                id = DocumentsContract.getDocumentId(u);
            } catch (IllegalArgumentException e) {
                try {
                    id = DocumentsContract.getTreeDocumentId(u);
                } catch (IllegalArgumentException e2) {
                    Log.w(TAG, "not a document: " + u);
                }
            }
            String path = id != null ? documentIdToPath(id) : null;
            if (path != null && new File(path).canRead()) return path;
            Log.w(TAG, "can't read " + path + " directly, copying it");
        }
        /* other apps' providers (a frontend's FileProvider, the media store) often
         * tell the real path; with All files access it can be read in place, so
         * the battery save stays next to the ROM */
        String real = realPath(c, u);
        if (real != null) return real;
        return copyIn(c, u);
    }

    /** "primary:ROMs/gba" -> /storage/emulated/0/ROMs/gba; other volumes are under /storage. */
    static String documentIdToPath(String id) {
        int colon = id.indexOf(':');
        if (colon < 0) return null;
        String volume = id.substring(0, colon), rest = id.substring(colon + 1);
        String root = "primary".equalsIgnoreCase(volume)
                ? Environment.getExternalStorageDirectory().getAbsolutePath()
                : "/storage/" + volume;
        return rest.isEmpty() ? root : root + "/" + rest;
    }

    private static String realPath(Context c, Uri u) {
        try (Cursor cur = c.getContentResolver().query(u, new String[] { "_data" }, null, null, null)) {
            if (cur != null && cur.moveToFirst() && !cur.isNull(0)) {
                String p = cur.getString(0);
                if (p != null && new File(p).canRead()) return p;
            }
        } catch (RuntimeException e) {
            /* no such column: fine */
        }
        /* FileProviders often keep the path in the URI: content://<authority>/<root>/storage/emulated/0/... */
        String up = u.getPath();
        if (up != null) {
            int i = up.indexOf("/storage/");
            if (i >= 0 && new File(up.substring(i)).canRead()) return up.substring(i);
        }
        return null;
    }

    /** Another app's file without a path: a copy in the app's folder (its saves stay there too). */
    private static String copyIn(Context c, Uri u) {
        String name = "game.gba";
        try (Cursor cur = c.getContentResolver().query(u, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cur != null && cur.moveToFirst() && cur.getString(0) != null) name = cur.getString(0).replace('/', '_');
        } catch (RuntimeException e) {
            Log.w(TAG, "no name for " + u + ": " + e);
        }
        File dir = c.getExternalFilesDir("launched");
        if (dir == null) dir = new File(c.getFilesDir(), "launched");
        dir.mkdirs();
        File out = new File(dir, name);
        try (InputStream is = c.getContentResolver().openInputStream(u); OutputStream os = new FileOutputStream(out)) {
            if (is == null) return null;
            byte[] buf = new byte[1 << 16];
            for (int n; (n = is.read(buf)) > 0; ) os.write(buf, 0, n);
        } catch (Exception e) {
            Log.e(TAG, "could not copy " + u + ": " + e);
            return null;
        }
        Log.i(TAG, "copied " + u + " to " + out);
        return out.getAbsolutePath();
    }
}
