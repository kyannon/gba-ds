package io.github.kyannon.gbads;

import android.app.ActivityManager;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;

/**
 * The game, started in the launcher's own task instead of one of its own:
 * on a device with one display (a phone, a Surface Duo; issue #48). A Surface
 * Duo opens a new task on one screen, so the game started in its own task
 * (as {@link GbaDsActivity} is) left an app the player had spanned over both
 * screens on one of them. In the task the launcher was opened in, the game
 * keeps the launcher's window, spanned or not.
 *
 * Everything else is GbaDsActivity's. The manifest gives this one the app's
 * usual task affinity and singleTop, where GbaDsActivity has a task of its own
 * (needed with two displays: GammaOS put the launcher's task on either).
 */
public class GbaDsHereActivity extends GbaDsActivity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        showInRecents();
    }

    /**
     * The task's first activity was the launcher, which is left out of
     * Recents (excludeFromRecents: with two displays the game has a task of
     * its own, which is the one Recents shows). Here the game is in that
     * task, so it's put back in.
     */
    private void showInRecents() {
        try {
            ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
            int mine = getTaskId();
            for (ActivityManager.AppTask t : am.getAppTasks()) {
                ActivityManager.RecentTaskInfo info = t.getTaskInfo();
                @SuppressWarnings("deprecation")
                int id = Build.VERSION.SDK_INT >= 29 ? info.taskId : info.id;
                if (id == mine) {
                    t.setExcludeFromRecents(false);
                    return;
                }
            }
        } catch (RuntimeException e) {
            LauncherActivity.launchLog(this, "couldn't put the game in Recents: " + e);
        }
    }
}
