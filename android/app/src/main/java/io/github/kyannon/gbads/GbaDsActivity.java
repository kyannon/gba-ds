package io.github.kyannon.gbads;

import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInstaller;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.text.InputType;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
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

    @Override
    protected void onCreate(Bundle state) {
        /* before SDL starts the native thread: where config, states and the log go */
        File home = dataFolder();
        try {
            Os.setenv("GBADS_HOME", home.getAbsolutePath(), true);
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
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LauncherActivity.launchLog(this, mBrokenLibraries ? "native libraries didn't load (SDL shows why)" : "native libraries loaded");
        if (!mBrokenLibraries) openSecondScreen();
        IntentFilter f = new IntentFilter(INSTALL_STATUS);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(installStatus, f, Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(installStatus, f);
    }

    /**
     * "GBA DS" on the internal storage, where people can reach it (cheats,
     * overlays, screenshots, save states), laid out like the ROCKNIX install
     * folder. Needs All files access; without it, the app's own folder.
     * Data from builds that used the app's folder is copied over once.
     */
    private File dataFolder() {
        File appDir = getExternalFilesDir(null);
        if (appDir == null) appDir = getFilesDir();
        File shared = new File(Environment.getExternalStorageDirectory(), "GBA DS");
        boolean canShare = Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
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

    private void openSecondScreen() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        int mine = getWindowManager().getDefaultDisplay().getDisplayId();
        Display other = null;
        for (Display d : dm.getDisplays()) {
            Point size = new Point();
            d.getRealSize(size);
            Log.i(TAG, "display " + d.getDisplayId() + " \"" + d.getName() + "\" " + size.x + "x" + size.y
                    + " rotation " + d.getRotation() + " flags 0x" + Integer.toHexString(d.getFlags())
                    + (d.getDisplayId() == mine ? " (game activity)" : ""));
            if (d.getDisplayId() != mine && other == null) other = d;
        }
        if (other == null) {
            Log.i(TAG, "only one display: no second screen");
            return;
        }
        Point size = new Point();
        other.getRealSize(size);
        SecondScreen.nativeSetDisplay(size.x, size.y);
        second = new SecondScreen(this, other);
        try {
            second.show();
            Log.i(TAG, "second screen on display " + other.getDisplayId() + " (" + size.x + "x" + size.y + ")");
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

    @Override
    protected void onDestroy() {
        try {
            unregisterReceiver(installStatus);
        } catch (IllegalArgumentException ignored) {
        }
        if (second != null) {
            second.dismiss();
            second = null;
        }
        boolean finishing = isFinishing();
        super.onDestroy();
        /* the native side keeps its state in statics: start fresh next time
         * (with the game a frontend asked for, if that's why we closed) */
        if (finishing) {
            if (switchTo != null) startActivity(switchTo);
            System.exit(0);
        }
    }
}
