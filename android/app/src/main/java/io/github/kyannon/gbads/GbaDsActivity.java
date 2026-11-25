package io.github.kyannon.gbads;

import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInstaller;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.LinearLayout;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The game. SDL draws on the display this activity is on; the other display
 * gets a {@link SecondScreen} (a Presentation) that the native code draws
 * into.
 *
 * Intent extra "rom": a ROM path to play (e.g. adb shell am start -n
 * io.github.kyannon.gbads/.LauncherActivity --es rom /storage/emulated/0/ROMs/gba/x.gba).
 */
public class GbaDsActivity extends SDLActivity {
    private static final String TAG = "GBADS";
    private SecondScreen second;

    /** The game activity in this process, if one is running (this class or
     * {@link GbaDsHereActivity}): the launcher sends a new request to it,
     * in its task, rather than starting a second one. */
    static volatile GbaDsActivity running;

    /**
     * SDL turns a gamepad's BACK key into the controller button it calls
     * "back", which is also what SELECT is, so a handheld with both keys (the
     * AYN Thor) gives the game one button for the two: Back counted as Select
     * (a hotkey remap took it for Select; in a game it pressed Select). When
     * the pad has a Select key of its own, Back is sent on as the keyboard
     * Back key instead, which the native side reads as HOME (ui.c). A pad with
     * only one of the two keeps SDL's way.
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && padHasBackAndSelect(event)) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private static boolean padHasBackAndSelect(KeyEvent event) {
        InputDevice d = event.getDevice();
        if (d == null) return false;
        int pad = InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK | InputDevice.SOURCE_DPAD;
        if ((d.getSources() & pad) == 0) return false; /* (the system's own Back key and a mouse's are left alone) */
        boolean[] has = d.hasKeys(KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_BUTTON_SELECT);
        return has.length == 2 && has[0] && has[1];
    }

    @Override
    protected void onCreate(Bundle state) {
        running = this;
        /* before SDL starts the native thread: where config, states and the log go */
        File home = dataFolder();
        try {
            Os.setenv("GBADS_HOME", home.getAbsolutePath(), true);
            /* data-folder.cfg there: the folder the player chose instead (android.c android_data_setup) */
            Os.setenv("GBADS_APP_FILES", getFilesDir().getAbsolutePath(), true);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv: " + e);
        }
        Log.i(TAG, "GBA DS " + BuildConfig.VERSION_NAME + ", data in " + home);
        /* launch.log: the steps up to the native side's own log, so a start
         * that dies before gba-ds.log exists can still be followed */
        LauncherActivity.launchLog(this, "starting GBA DS " + BuildConfig.VERSION_NAME + " on " + Build.MANUFACTURER + " "
                + Build.MODEL + " (" + Build.HARDWARE + "), Android " + Build.VERSION.RELEASE + " (API "
                + Build.VERSION.SDK_INT + "), ABIs " + java.util.Arrays.toString(Build.SUPPORTED_ABIS)
                + ", data in " + home);
        /* before the window is laid out: on a Surface Duo a landscape lock
         * here takes a spanned app back to one screen (issue #48) */
        setRequestedOrientation(orientationWanted());
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LauncherActivity.launchLog(this, mBrokenLibraries ? "native libraries didn't load (SDL shows why)" : "native libraries loaded");
        if (!mBrokenLibraries) {
            openSecondScreen();
            watchDisplays();
        }
        IntentFilter f = new IntentFilter(INSTALL_STATUS);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(installStatus, f, Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(installStatus, f);
    }

    /**
     * "GBA DS" on the internal storage, where people can reach it (cheats,
     * overlays, screenshots, save states), laid out like the ROCKNIX install
     * folder. Needs All files access; without it, the app's own folder.
     * Data from builds that used the app's folder is copied over once.
     * This is the default: a folder the player chose instead (Settings >
     * Folders) is set up by the native side (android_data_setup), and then
     * "GBA DS" isn't made.
     */
    private File dataFolder() {
        File appDir = getExternalFilesDir(null);
        if (appDir == null) appDir = getFilesDir();
        File shared = new File(Environment.getExternalStorageDirectory(), "GBA DS");
        boolean canShare = Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
        String now = LauncherActivity.dataFolderSetting(this, "data_dir_now");
        if (canShare && now != null && !new File(now).equals(shared) && !shared.isDirectory()) return shared;
        if (!canShare || !(shared.isDirectory() || shared.mkdirs())) {
            Log.w(TAG, "no All files access: data stays in " + appDir);
            appDir.mkdirs();
            return appDir;
        }
        File oldStates = new File(appDir, "states"), newStates = new File(shared, "states");
        if (oldStates.isDirectory() && !newStates.exists()) {
            Log.i(TAG, "moving " + oldStates + " to " + newStates);
            if (copyTree(oldStates, newStates)) deleteTree(oldStates);
        }
        return shared;
    }

    private static boolean copyTree(File from, File to) {
        if (from.isDirectory()) {
            if (!to.isDirectory() && !to.mkdirs()) return false;
            File[] list = from.listFiles();
            boolean ok = true;
            if (list != null) for (File f : list) ok &= copyTree(f, new File(to, f.getName()));
            return ok;
        }
        try (InputStream is = new FileInputStream(from); OutputStream os = new FileOutputStream(to)) {
            byte[] buf = new byte[1 << 16];
            for (int n; (n = is.read(buf)) > 0; ) os.write(buf, 0, n);
            return true;
        } catch (IOException e) {
            Log.e(TAG, "copying " + from + ": " + e);
            return false;
        }
    }

    private static void deleteTree(File f) {
        File[] list = f.listFiles();
        if (list != null) for (File c : list) deleteTree(c);
        f.delete();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    private String currentRom; /* the game started with (null: the game list) */
    private Intent switchTo;   /* a frontend asked for another game: restart with it */

    @Override
    protected String[] getArguments() {
        String rom = getIntent() != null ? getIntent().getStringExtra("rom") : null;
        if (rom != null && !rom.isEmpty()) {
            Log.i(TAG, "rom from intent: " + rom);
            currentRom = rom;
            return new String[] { rom };
        }
        return new String[0];
    }

    /** Already running and started again (app icon, or a frontend with a game). */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String rom = intent.getStringExtra("rom");
        if (rom == null || rom.equals(currentRom)) return; /* just brought to the front */
        Log.i(TAG, "asked for another game (" + rom + "): closing this one first");
        switchTo = new Intent(this, LauncherActivity.class).putExtra("rom", rom).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        /* the game saves its resume point and returns from main; SDL then finishes this activity */
        SDLActivity.nativeSendQuit();
    }

    /* ------------------------------------------------------------ orientation */

    /** Another display beside this activity's (the one openSecondScreen uses). */
    private boolean hasSecondDisplay() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        int mine = getWindowManager().getDefaultDisplay().getDisplayId();
        for (Display d : dm.getDisplays()) if (d.getDisplayId() != mine) return true;
        return false;
    }

    /**
     * Two displays (RG DS, AYN Thor, Retroid Pocket Duo...): landscape, as
     * before. One display (a phone, or a Surface Duo, whose two screens are
     * one display; issue #48): whichever way the player holds it, both ways
     * round (a controller's cable can be on either side), keeping the
     * player's rotation lock. The native side lays the screens out from the
     * window's shape (ui.c layout_screens).
     */
    private int orientationWanted() {
        boolean two = hasSecondDisplay();
        String mode = rotationMode();
        int o;
        if (two) o = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        else if (mode.equals("portrait")) o = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
        else if (mode.equals("landscape")) o = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
        else o = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
        Log.i(TAG, two ? "two displays: landscape"
                : "one display: " + (mode.equals("auto") ? "any orientation (the rotation lock is kept)" : mode + " (Settings > Rotation)"));
        return o;
    }

    /**
     * Settings > Rotation (issue #60): "auto" (follow the phone, the default),
     * "portrait" or "landscape", kept in rotation.cfg in the app's own storage
     * (read here before the native side starts; the native side writes it, as
     * with data-folder.cfg). Only used with one display.
     */
    private String rotationMode() {
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(new File(getFilesDir(), "rotation.cfg")))) {
            String l = r.readLine();
            if (l != null) {
                l = l.trim();
                if (l.equals("portrait") || l.equals("landscape")) return l;
            }
        } catch (java.io.IOException e) {
            /* none: auto */
        }
        return "auto";
    }

    /** Called by the native side when Settings > Rotation changes (the file is written already). */
    public void applyRotation(String mode) {
        runOnUiThread(() -> setRequestedOrientation(orientationWanted()));
    }

    /** SDL asks when it makes the window (its hint: LandscapeLeft, ui.c). */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(orientationWanted());
    }

    /* ------------------------------------------------------------ a display comes or goes (issue #90) */

    /* The game reads the displays once, and the native side lays the screens
     * out once (ui.c), so a second display that appears (a USB-C screen
     * plugged in, or woken, after the game started) or goes is taken in by
     * starting the game again with the new layout: it saves the player's
     * place first (nativeRelayout) and loads it on the way back in, whatever
     * Save on quit and Auto-load say. */
    static native void nativeRelayout();

    private final Handler displayHandler = new Handler(Looper.getMainLooper());
    private DisplayManager.DisplayListener displayListener;
    private boolean layoutTwo;        /* whether a second display was there when the game started */
    private int layoutSecondId = -1;  /* the display it was given, and its size then */
    private final Point layoutSecondSize = new Point();
    private boolean relaunching;
    private boolean inBackground;

    private final Runnable displayCheck = this::checkDisplays;

    private void watchDisplays() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        layoutTwo = hasSecondDisplay();
        int mine = getWindowManager().getDefaultDisplay().getDisplayId();
        for (Display d : dm.getDisplays()) {
            if (d.getDisplayId() != mine) {
                layoutSecondId = d.getDisplayId();
                d.getRealSize(layoutSecondSize);
                break;
            }
        }
        displayListener = new DisplayManager.DisplayListener() {
            @Override
            public void onDisplayAdded(int id) {
                Log.i(TAG, "display " + id + " added");
                displayChanged();
            }

            @Override
            public void onDisplayRemoved(int id) {
                Log.i(TAG, "display " + id + " removed");
                displayChanged();
            }

            @Override
            public void onDisplayChanged(int id) {
                if (id == layoutSecondId) displayChanged();
            }
        };
        dm.registerDisplayListener(displayListener, displayHandler);
    }

    /** Events come in bursts (a display added, then changed, then its state): look once they settle. */
    private void displayChanged() {
        displayHandler.removeCallbacks(displayCheck);
        displayHandler.postDelayed(displayCheck, 1000);
    }

    private void checkDisplays() {
        if (relaunching || isFinishing() || isDestroyed()) return;
        if (inBackground) return; /* (Android won't start an activity from there: onStart looks again) */
        boolean two = hasSecondDisplay();
        boolean sizeChanged = false;
        if (two && layoutTwo && layoutSecondId >= 0) {
            DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
            Display d = dm.getDisplay(layoutSecondId);
            if (d == null) {
                sizeChanged = true; /* (the one it was using went, another is there) */
            } else {
                Point size = new Point();
                d.getRealSize(size);
                sizeChanged = !size.equals(layoutSecondSize.x, layoutSecondSize.y);
            }
        }
        if (two == layoutTwo && !sizeChanged) return;
        LauncherActivity.launchLog(this, "displays changed (" + (layoutTwo ? "two" : "one") + " -> " + (two ? "two" : "one")
                + (sizeChanged ? ", the second one's size or identity changed" : "") + "): restarting the game with the new layout");
        relaunching = true;
        nativeRelayout();
        if (currentRom != null) {
            /* read by the native side at the next start (android_relayout_resume) */
            try (FileOutputStream o = new FileOutputStream(new File(getFilesDir(), "relayout.flag"))) {
                o.write('1');
            } catch (IOException e) {
                Log.e(TAG, "relayout.flag: " + e);
            }
        }
        Intent again = new Intent(this, LauncherActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (currentRom != null) again.putExtra("rom", currentRom);
        switchTo = again;
        /* the game saves its resume point and returns from main; SDL then finishes this activity */
        SDLActivity.nativeSendQuit();
    }

    private void openSecondScreen() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        int mine = getWindowManager().getDefaultDisplay().getDisplayId();
        Display other = null;
        /* Every display goes to launch.log (a USB-C screen such as the Retroid
         * Dual Screen Add-on on an AYN Odin 2 may be listed with other flags
         * or states than a built-in one, issue #65), then the one to use is
         * chosen: not this activity's, and of those the one Android marks for
         * presentations first, then one that is on, then the first. */
        int bestScore = -1;
        for (Display d : dm.getDisplays()) {
            Point size = new Point();
            d.getRealSize(size);
            int flags = d.getFlags();
            LauncherActivity.launchLog(this, "display " + d.getDisplayId() + " \"" + d.getName() + "\" " + size.x + "x" + size.y
                    + " rotation " + d.getRotation() + " flags 0x" + Integer.toHexString(flags)
                    + ((flags & Display.FLAG_PRESENTATION) != 0 ? " (presentation)" : "") + " state " + d.getState()
                    + (d.getDisplayId() == mine ? " (game activity)" : ""));
            if (d.getDisplayId() == mine) continue;
            int score = ((flags & Display.FLAG_PRESENTATION) != 0 ? 2 : 0) + (d.getState() == Display.STATE_ON ? 1 : 0);
            if (score > bestScore) {
                bestScore = score;
                other = d;
            }
        }
        if (other == null) {
            LauncherActivity.launchLog(this, "only one display: no second screen");
            return;
        }
        Point size = new Point();
        other.getRealSize(size);
        SecondScreen.nativeSetDisplay(size.x, size.y);
        /* pixels per inch of each (screen_match = size; some devices report
         * made-up values, which the native side leaves out) */
        DisplayMetrics mineDm = new DisplayMetrics(), otherDm = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(mineDm);
        other.getRealMetrics(otherDm);
        Log.i(TAG, "display ppi: " + mineDm.xdpi + " x " + mineDm.ydpi + " (game activity), " + otherDm.xdpi + " x " + otherDm.ydpi + " (second)");
        SecondScreen.nativeSetPpi((mineDm.xdpi + mineDm.ydpi) / 2, (otherDm.xdpi + otherDm.ydpi) / 2);
        second = new SecondScreen(this, other);
        try {
            second.show();
            LauncherActivity.launchLog(this, "second screen on display " + other.getDisplayId() + " (" + size.x + "x" + size.y + ")");
        } catch (WindowManager.InvalidDisplayException e) {
            Log.e(TAG, "second screen: " + e);
            SecondScreen.nativeSetDisplay(0, 0);
            second = null;
        }
    }

    /* ------------------------------------------------------------ ROM folder picker */

    private static final int PICK_FOLDER = 4711;

    static native void nativeFolderPicked(String path);

    /* ------------------------------------------------------------ RetroAchievements sign-in */

    /** user/password, or nulls when cancelled. */
    static native void nativeRaCredentials(String user, String password);

    /**
     * Called by the native side (game thread), from the game list's Settings:
     * a dialog for the RetroAchievements account. The password goes to the
     * native side once, which signs in with it and keeps only the token.
     */
    public void raSignInDialog(String user) {
        runOnUiThread(() -> {
            int pad = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 20, getResources().getDisplayMetrics());
            LinearLayout box = new LinearLayout(this);
            box.setOrientation(LinearLayout.VERTICAL);
            box.setPadding(pad, pad / 2, pad, 0);
            EditText name = new EditText(this);
            name.setHint("Username");
            name.setSingleLine(true);
            name.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            name.setImeOptions(EditorInfo.IME_ACTION_NEXT);
            if (user != null) name.setText(user);
            EditText pass = new EditText(this);
            pass.setHint("Password");
            pass.setSingleLine(true);
            pass.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            pass.setImeOptions(EditorInfo.IME_ACTION_DONE);
            box.addView(name);
            box.addView(pass);
            final boolean[] answered = { false };
            AlertDialog d = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("RetroAchievements")
                    .setMessage("Sign in with your retroachievements.org account. GBA DS keeps a sign-in token, not your password.")
                    .setView(box)
                    .setPositiveButton("Sign in", (dialog, which) -> {
                        answered[0] = true;
                        nativeRaCredentials(name.getText().toString().trim(), pass.getText().toString());
                    })
                    .setNegativeButton("Cancel", (dialog, which) -> {
                        answered[0] = true;
                        nativeRaCredentials(null, null);
                    })
                    .create();
            d.setOnDismissListener(dialog -> {
                if (!answered[0]) nativeRaCredentials(null, null); /* BACK, or a tap outside */
            });
            d.show();
            (name.getText().length() > 0 ? pass : name).requestFocus();
        });
    }

    /** Called by the native side (game thread): opens Android's folder picker. */
    public void pickRomFolder() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try {
                startActivityForResult(i, PICK_FOLDER);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no folder picker: " + e);
                nativeFolderPicked("");
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FOLDER) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        String path = "";
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            path = treeUriToPath(data.getData());
            Log.i(TAG, "folder picked: " + data.getData() + " -> " + path);
        }
        nativeFolderPicked(path);
    }

    /** A picked folder (tree URI) as a plain path (readable with All files access). */
    private static String treeUriToPath(Uri uri) {
        try {
            String path = LauncherActivity.documentIdToPath(DocumentsContract.getTreeDocumentId(uri));
            return path != null ? path : "";
        } catch (IllegalArgumentException e) {
            return "";
        }
    }

    /* ------------------------------------------------------------ updates */

    private static final String INSTALL_STATUS = "io.github.kyannon.gbads.INSTALL_STATUS";

    /** 1 asking to confirm, 2 installed, -1 failed, -2 cancelled, -3 another key, -4 no room, -5 older build (android.h). */
    static native void nativeInstallResult(int state, String message);

    /**
     * Called by the native side (the update's worker thread): hands a
     * downloaded APK to Android's package installer, which then asks the
     * player to confirm (installStatus). Returns "" once it's handed over,
     * else why it couldn't be.
     */
    public String installApk(String path) {
        PackageInstaller pi = getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(getPackageName());
        int id = -1;
        try {
            id = pi.createSession(params);
            File apk = new File(path);
            try (PackageInstaller.Session s = pi.openSession(id)) {
                try (InputStream in = new FileInputStream(apk); OutputStream out = s.openWrite("gba-ds.apk", 0, apk.length())) {
                    byte[] buf = new byte[1 << 16];
                    for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
                    s.fsync(out);
                }
                Intent i = new Intent(INSTALL_STATUS).setPackage(getPackageName());
                int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
                s.commit(PendingIntent.getBroadcast(this, id, i, flags).getIntentSender());
            }
            Log.i(TAG, "update: " + path + " handed to the package installer (session " + id + ")");
            return "";
        } catch (IOException | RuntimeException e) {
            Log.e(TAG, "update: installer: " + e);
            if (id >= 0) {
                try {
                    pi.abandonSession(id);
                } catch (RuntimeException ignored) {
                }
            }
            String m = e.getMessage();
            return m != null && !m.isEmpty() ? m : e.getClass().getSimpleName();
        }
    }

    private final BroadcastReceiver installStatus = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int st = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            Log.i(TAG, "update: install status " + st + (msg != null ? " (" + msg + ")" : ""));
            if (st != PackageInstaller.STATUS_SUCCESS && msg != null && msg.contains("DOWNGRADE")) {
                /* the copy installed is a later build (a test APK built after the release) */
                nativeInstallResult(-5, msg);
                return;
            }
            switch (st) {
            case PackageInstaller.STATUS_PENDING_USER_ACTION: {
                @SuppressWarnings("deprecation")
                Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm == null) {
                    nativeInstallResult(-1, "Android's installer didn't ask to confirm");
                    return;
                }
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    startActivity(confirm);
                    nativeInstallResult(1, null);
                } catch (ActivityNotFoundException | SecurityException e) {
                    nativeInstallResult(-1, "Android's installer couldn't open (" + e.getMessage() + ")");
                }
                return;
            }
            case PackageInstaller.STATUS_SUCCESS:
                nativeInstallResult(2, null);
                return;
            case PackageInstaller.STATUS_FAILURE_ABORTED:
                nativeInstallResult(-2, msg);
                return;
            case PackageInstaller.STATUS_FAILURE_CONFLICT:
            case PackageInstaller.STATUS_FAILURE_INCOMPATIBLE:
                nativeInstallResult(-3, msg);
                return;
            case PackageInstaller.STATUS_FAILURE_STORAGE:
                nativeInstallResult(-4, msg);
                return;
            default:
                nativeInstallResult(-1, msg);
            }
        }
    };

    /* ------------------------------------------------------------ in the background (issue #28) */

    /* Android leaves a Presentation up while its activity is in the
     * background: the second display stayed GBA DS's (and awake) until the
     * app closed. So it's hidden while the app can't be seen and shown
     * again when it can. Hiding takes its surface away (the native side lets
     * go of it: nativeSetSurface(null)) and showing brings a new one, which
     * the native side draws on again (ui.c retries a redraw until the window
     * is there). Dialog.hide() keeps the window, so show() can't fail on a
     * display that's still there. */
    private boolean secondHidden;

    @Override
    protected void onStart() {
        super.onStart();
        inBackground = false;
        if (displayListener != null) displayChanged();
        if (second != null && secondHidden) {
            secondHidden = false;
            try {
                second.show();
                Log.i(TAG, "second screen shown again (the app is in the foreground)");
            } catch (RuntimeException e) { /* (its display gone meanwhile: Android cancels it) */
                Log.e(TAG, "second screen: " + e);
            }
        }
    }

    @Override
    protected void onStop() {
        super.onStop();
        inBackground = true;
        if (second != null && !secondHidden && !isFinishing()) {
            second.hide();
            secondHidden = true;
            Log.i(TAG, "second screen hidden (the app is in the background)");
        }
    }

    @Override
    protected void onDestroy() {
        try {
            unregisterReceiver(installStatus);
        } catch (IllegalArgumentException ignored) {
        }
        displayHandler.removeCallbacks(displayCheck);
        if (displayListener != null) {
            ((DisplayManager) getSystemService(Context.DISPLAY_SERVICE)).unregisterDisplayListener(displayListener);
            displayListener = null;
        }
        if (second != null) {
            second.dismiss();
            second = null;
        }
        boolean finishing = isFinishing();
        if (running == this) running = null;
        super.onDestroy();
        /* the native side keeps its state in statics: start fresh next time
         * (with the game a frontend asked for, if that's why we closed) */
        if (finishing) {
            if (switchTo != null) startActivity(switchTo);
            System.exit(0);
        }
    }
}
