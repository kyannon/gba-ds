package io.github.kyannon.gbads;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * The RetroAchievements server a local proxy app (RAOfflineProxy) asks GBA DS
 * to use, kept in a file in the app's private folder (not in the shared
 * "GBA DS" folder, where any app with storage access could change it). src/ra.c
 * reads it at the next game start or sign-in. Only plain http to this device
 * is accepted, so nothing can send the player's sign-in to another server.
 */
final class RetroAchievementsHostOverride {
    static final String FILE_NAME = "ra-host-override";

    private RetroAchievementsHostOverride() {
    }

    /** "http://127.0.0.1:8080" for an accepted value, else null. */
    static String normalize(String value) {
        if (value == null) return null;
        String text = value.trim();
        if (text.isEmpty()) return null;
        if (!text.contains("://")) text = "http://" + text;
        URI uri;
        try {
            uri = new URI(text);
        } catch (URISyntaxException e) {
            return null;
        }
        String scheme = uri.getScheme() == null ? null : uri.getScheme().toLowerCase(Locale.US);
        String host = uri.getHost() == null ? null : uri.getHost().toLowerCase(Locale.US);
        if (!"http".equals(scheme) || host == null) return null;
        if (!host.equals("127.0.0.1") && !host.equals("localhost") && !host.equals("[::1]")) return null;
        if (uri.getPort() < 1 || uri.getPort() > 65535) return null;
        if (uri.getUserInfo() != null || uri.getRawQuery() != null || uri.getRawFragment() != null) return null;
        String path = uri.getRawPath();
        if (path != null && !path.isEmpty() && !path.equals("/") && !path.equals("/dorequest.php")) return null;
        return "http://" + host + ":" + uri.getPort();
    }

    static boolean set(File dir, String value) {
        String host = normalize(value);
        if (host == null) return false;
        File part = new File(dir, FILE_NAME + ".part");
        try (FileOutputStream os = new FileOutputStream(part)) {
            os.write((host + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            part.delete();
            return false;
        }
        if (!part.renameTo(new File(dir, FILE_NAME))) {
            part.delete();
            return false;
        }
        return true;
    }

    static void clear(File dir) {
        new File(dir, FILE_NAME).delete();
    }
}
