package io.github.kyannon.gbads;

import android.app.Presentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The second display: a full-screen SurfaceView whose Surface the native
 * code draws into (map, logo, menus), and whose touches go to the native
 * pointer queue.
 */
public class SecondScreen extends Presentation implements SurfaceHolder.Callback {
    private static final String TAG = "GBADS";
    private SurfaceView view;

    static native void nativeSetDisplay(int width, int height);
    /* the displays' pixels per inch: the activity's and this one's */
    static native void nativeSetPpi(float app, float second);
    static native void nativeSetSurface(Surface surface);
    static native void nativeTouch(int kind, float x, float y);

    public SecondScreen(Context context, Display display) {
        super(context, display);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Window w = getWindow();
        /* never take key focus: the buttons must keep going to the game */
        w.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new SurfaceView(getContext());
        view.getHolder().addCallback(this);
        view.setOnTouchListener(this::onTouch);
        setContentView(view);
        hideSystemBars();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        Window w = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    private boolean onTouch(View v, MotionEvent e) {
        int kind;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: kind = 1; break;
            case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: kind = 2; break;
            case MotionEvent.ACTION_MOVE: kind = 3; break;
            default: return true;
        }
        float x = e.getX() / Math.max(1, v.getWidth()), y = e.getY() / Math.max(1, v.getHeight());
        if (kind != 3) {
            Log.i(TAG, "second screen touch " + (kind == 1 ? "down" : "up") + " at "
                    + (int) e.getX() + "," + (int) e.getY() + " of " + v.getWidth() + "x" + v.getHeight());
        }
        nativeTouch(kind, x, y);
        return true;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "second screen surface " + width + "x" + height);
        nativeSetSurface(holder.getSurface());
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSetSurface(null);
    }
}
