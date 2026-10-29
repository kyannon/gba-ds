package io.github.kyannon.gbads;

import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * HTTP for RetroAchievements (src/ra.c) and updates (src/update.c through
 * src/http.c), via android.c, called from the native side's background
 * threads: Android has no curl, and this way HTTPS uses the system's
 * certificates.
 */
final class Http {
    private static final String TAG = "GBADS";

    /** Set from the native side (android_http_cancel): stops a download to a file. */
    static volatile boolean cancelDownloads;

    private Http() {
    }

    /**
     * One request. post null: GET. outPath non-null: the body goes to that
     * file (written whole or not at all) and null is returned. status[0]
     * gets the HTTP status, or -1 when there was no answer.
     */
    static byte[] request(String url, String contentType, byte[] post, String userAgent, String outPath, int[] status) {
        status[0] = -1;
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(15000);
            c.setReadTimeout(30000);
            c.setInstanceFollowRedirects(true);
            c.setUseCaches(false);
            if (userAgent != null) c.setRequestProperty("User-Agent", userAgent);
            if (post != null) {
                c.setDoOutput(true);
                c.setRequestMethod("POST");
                c.setRequestProperty("Content-Type", contentType != null ? contentType : "application/x-www-form-urlencoded");
                c.setFixedLengthStreamingMode(post.length);
                try (OutputStream os = c.getOutputStream()) {
                    os.write(post);
                }
            }
            int code = c.getResponseCode();
            status[0] = code;
            InputStream is = code >= 400 ? c.getErrorStream() : c.getInputStream();
            if (outPath != null) {
                if (code / 100 != 2 || is == null) return null;
                File part = new File(outPath + ".part");
                try (InputStream in = is; OutputStream os = new FileOutputStream(part)) {
                    byte[] buf = new byte[1 << 16];
                    for (int n; (n = in.read(buf)) > 0; ) {
                        if (cancelDownloads) throw new IOException("cancelled");
                        os.write(buf, 0, n);
                    }
                }
                if (!part.renameTo(new File(outPath))) part.delete();
                return null;
            }
            if (is == null) return new byte[0];
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            try (InputStream in = is) {
                copy(in, out);
            }
            return out.toByteArray();
        } catch (IOException | RuntimeException e) {
            /* the host only: never anything a request carries */
            Log.w(TAG, "http: no answer from " + hostOf(url) + ": " + e);
            status[0] = -1;
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 14];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
    }

    private static String hostOf(String url) {
        try {
            return new URL(url).getHost();
        } catch (IOException e) {
            return "?";
        }
    }
}
