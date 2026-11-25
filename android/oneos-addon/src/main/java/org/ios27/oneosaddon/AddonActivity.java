package org.ios27.oneosaddon;

import android.app.Activity;
import android.app.DownloadManager;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.Settings;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class AddonActivity extends Activity {
    private static final String ONEOS = "com.fahadali.oneos";
    private static final String RUNTIME = "org.ios27.host";
    private final ExecutorService verifier = Executors.newSingleThreadExecutor();
    private DownloadManager downloads;
    private SharedPreferences state;
    private Artifact archive;
    private Artifact kit;
    private TextView status;
    private Button install;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            refresh();
            ui.postDelayed(this, 2000);
        }
    };

    private static final class Artifact {
        final String key, title, file, digest;
        final Uri url;
        final long size;
        Button download;
        TextView detail;
        long verifying = -1;

        Artifact(String key, String title, JSONObject item) throws Exception {
            this.key = key;
            this.title = title;
            file = item.getString("file");
            digest = item.getString("sha256");
            url = Uri.parse(item.getString("url"));
            size = item.getLong("size");
            if (!file.matches("[A-Za-z0-9._-]+") || !digest.matches("[0-9a-f]{64}") || size <= 0) {
                throw new IOException("Invalid artifact catalog");
            }
        }
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        downloads = getSystemService(DownloadManager.class);
        state = getSharedPreferences("downloads", MODE_PRIVATE);
        ScrollView scroll = new ScrollView(this);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (20 * getResources().getDisplayMetrics().density);
        content.setPadding(padding, padding, padding, padding);
        scroll.addView(content);
        setContentView(scroll);
        TextView title = text(content, "iOS 27 Add-on", 26);
        title.setAccessibilityHeading(true);
        text(content, "Apple iOS 27.0 Simulator archive and ios-27 compatibility runtime. "
                + "SpringBoard launch is still in development.", 16);
        status = text(content, "", 14);
        try (BufferedReader catalog = new BufferedReader(new InputStreamReader(
                getAssets().open("catalog.json"), StandardCharsets.UTF_8))) {
            JSONObject entries = new JSONObject(catalog.readLine());
            archive = new Artifact("archive", "Download Apple runtime · 8.05 GB", entries.getJSONObject("archive"));
            kit = new Artifact("kit", "Download compatibility kit", entries.getJSONObject("kit"));
            addDownload(content, archive);
            addDownload(content, kit);
            install = button(content, "Install verified compatibility kit", this::installKit);
            button(content, "Open installed compatibility runtime", () -> open(RUNTIME, "Compatibility runtime"));
            button(content, "Open OneOS", () -> open(ONEOS, "OneOS"));
            button(content, "Refresh download status", this::refresh);
        } catch (Exception error) {
            archive = kit = null;
            status.setText("Cannot open the build's artifact catalog.");
        }
    }

    @Override protected void onResume() {
        super.onResume();
        ui.post(poll);
    }

    @Override protected void onPause() {
        ui.removeCallbacks(poll);
        super.onPause();
    }

    @Override protected void onDestroy() {
        verifier.shutdownNow();
        super.onDestroy();
    }

    private TextView text(LinearLayout parent, String label, int size) {
        TextView view = new TextView(this);
        view.setText(label);
        view.setTextSize(size);
        parent.addView(view);
        return view;
    }

    private Button button(LinearLayout parent, String label, Runnable onClick) {
        Button view = new Button(this);
        view.setText(label);
        view.setOnClickListener(ignored -> onClick.run());
        parent.addView(view);
        return view;
    }

    private void addDownload(LinearLayout content, Artifact artifact) {
        artifact.download = button(content, artifact.title, () -> start(artifact));
        artifact.detail = text(content, "", 14);
    }

    private long id(Artifact artifact) { return state.getLong(artifact.key, -1); }
    private String verifiedKey(Artifact artifact) { return artifact.key + ":" + artifact.digest; }

    private void start(Artifact artifact) {
        File directory = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
        if (directory == null || directory.getUsableSpace() < artifact.size) {
            status.setText("Free at least " + artifact.size + " bytes of storage for this download.");
            return;
        }
        try {
            long oldId = id(artifact);
            if (oldId >= 0) downloads.remove(oldId);
            DownloadManager.Request request = new DownloadManager.Request(artifact.url)
                    .setTitle(artifact.title)
                    .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                    .setDestinationInExternalFilesDir(this, Environment.DIRECTORY_DOWNLOADS, artifact.file);
            if (artifact == archive) {
                request.setAllowedOverMetered(false).setAllowedOverRoaming(false);
            } else {
                request.setMimeType("application/vnd.android.package-archive");
            }
            long download = downloads.enqueue(request);
            state.edit().putLong(artifact.key, download).remove(verifiedKey(artifact)).apply();
            status.setText("");
            refresh();
        } catch (RuntimeException error) {
            status.setText("Could not queue the download. Check the source and storage, then retry.");
        }
    }

    private void refresh() {
        if (archive == null || kit == null) return;
        update(archive);
        install.setEnabled(update(kit));
    }

    private boolean update(Artifact artifact) {
        long download = id(artifact);
        try (Cursor cursor = downloads.query(new DownloadManager.Query().setFilterById(download))) {
            if (download < 0 || cursor == null || !cursor.moveToFirst()) {
                artifact.download.setEnabled(true);
                artifact.detail.setText("Ready to download");
                return false;
            }
            int result = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS));
            artifact.download.setEnabled(result == DownloadManager.STATUS_FAILED);
            if (result == DownloadManager.STATUS_SUCCESSFUL) {
                if (state.getLong(verifiedKey(artifact), -1) == download) {
                    artifact.detail.setText("SHA-256 verified");
                    return true;
                }
                artifact.detail.setText("Verifying SHA-256…");
                if (artifact.verifying != download) verify(artifact, download);
            } else if (result == DownloadManager.STATUS_FAILED) {
                int reason = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON));
                artifact.detail.setText("Download failed (" + reason + "). Tap download to retry.");
            } else if (result == DownloadManager.STATUS_PAUSED) {
                artifact.detail.setText(artifact == archive ? "Waiting for unmetered network" : "Download paused");
            } else {
                long bytes = cursor.getLong(cursor.getColumnIndexOrThrow(
                        DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR));
                artifact.detail.setText(String.format(Locale.ROOT, "%,d of %,d bytes", bytes, artifact.size));
            }
        }
        return false;
    }

    private void verify(Artifact artifact, long download) {
        artifact.verifying = download;
        verifier.execute(() -> {
            boolean matches = false;
            try (InputStream input = new ParcelFileDescriptor.AutoCloseInputStream(
                    downloads.openDownloadedFile(download))) {
                MessageDigest sha = MessageDigest.getInstance("SHA-256");
                byte[] buffer = new byte[65536];
                long size = 0;
                for (int read; (read = input.read(buffer)) != -1;) {
                    if (Thread.currentThread().isInterrupted()) return;
                    size += read;
                    if (size > artifact.size) break;
                    sha.update(buffer, 0, read);
                }
                StringBuilder digest = new StringBuilder(64);
                for (byte value : sha.digest()) {
                    digest.append(Character.forDigit((value & 255) >>> 4, 16));
                    digest.append(Character.forDigit(value & 15, 16));
                }
                matches = size == artifact.size && artifact.digest.contentEquals(digest);
            } catch (Exception ignored) {
                // An unreadable download has no verified receipt.
            }
            boolean valid = matches;
            runOnUiThread(() -> {
                if (isDestroyed() || id(artifact) != download) return;
                artifact.verifying = -1;
                if (valid) {
                    state.edit().putLong(verifiedKey(artifact), download).apply();
                } else {
                    downloads.remove(download);
                    state.edit().remove(artifact.key).remove(verifiedKey(artifact)).apply();
                    status.setText("Download verification failed. The file was removed.");
                }
                refresh();
            });
        });
    }

    private void installKit() {
        long download = id(kit);
        if (download < 0 || state.getLong(verifiedKey(kit), -1) != download) return;
        Uri file = downloads.getUriForDownloadedFile(download);
        if (file == null) return;
        try {
            if (!getPackageManager().canRequestPackageInstalls()) {
                startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + getPackageName())));
                return;
            }
            startActivity(new Intent(Intent.ACTION_VIEW).setDataAndType(file,
                    "application/vnd.android.package-archive").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION));
        } catch (ActivityNotFoundException error) {
            status.setText("Android's package installer could not be opened.");
        }
    }

    private void open(String packageName, String title) {
        Intent intent = getPackageManager().getLaunchIntentForPackage(packageName);
        if (intent != null) startActivity(intent);
        else status.setText(title + " is not installed.");
    }
}
