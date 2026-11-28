package io.github.kyannon.gbads;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * Lets a local RetroAchievements proxy app set or clear the server GBA DS talks
 * to (see RetroAchievementsHostOverride). SET carries the string extra "host";
 * the value is kept only if it is plain http to 127.0.0.1, localhost or [::1]
 * with a port. While it is set, GBA DS plays in casual mode; the player's own
 * hardcore setting is not touched, so it is back when the override is cleared.
 */
public final class RetroAchievementsHostOverrideReceiver extends BroadcastReceiver {
    private static final String TAG = "GBADS";
    static final String ACTION_SET_SUFFIX = ".action.SET_RETROACHIEVEMENTS_HOST_OVERRIDE";
    static final String ACTION_CLEAR_SUFFIX = ".action.CLEAR_RETROACHIEVEMENTS_HOST_OVERRIDE";
    static final String EXTRA_HOST = "host";

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (action == null) return;
        String prefix = context.getPackageName();
        if (action.equals(prefix + ACTION_SET_SUFFIX)) {
            boolean ok = RetroAchievementsHostOverride.set(context.getFilesDir(), intent.getStringExtra(EXTRA_HOST));
            Log.i(TAG, "RetroAchievements server override: " + (ok ? "set" : "refused"));
            if (!ok && isOrderedBroadcast()) setResultCode(Activity.RESULT_CANCELED);
        } else if (action.equals(prefix + ACTION_CLEAR_SUFFIX)) {
            RetroAchievementsHostOverride.clear(context.getFilesDir());
            Log.i(TAG, "RetroAchievements server override: cleared");
        }
    }
}
