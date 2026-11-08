package org.ios27.host;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.os.Bundle;
import android.os.Build;
import android.graphics.Color;
import android.graphics.Typeface;
import android.view.View;
import android.widget.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.concurrent.*;
import org.json.JSONObject;

public final class MainActivity extends Activity {
    private static final String DIRTYFRAG_PACKAGE = "df.root";
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private volatile Process running;
    private TextView status, output;
    private LinearLayout session;
    private ScrollView picker;
    private Button run, openSession, openDirtyFrag;
    private boolean sessionStarted;
    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView v = new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(color);
        v.setPadding(0, dp(8), 0, dp(8)); return v;
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(24), dp(24), dp(16)); root.setBackgroundColor(Color.rgb(15,22,32));
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(dp(24), dp(16) + insets.getSystemWindowInsetTop(), dp(24), dp(16) + insets.getSystemWindowInsetBottom()); return insets;
        });
        LinearLayout home = new LinearLayout(this);
        home.setOrientation(LinearLayout.VERTICAL);
        picker = new ScrollView(this);
        picker.addView(home);
        root.addView(picker);
        home.addView(text("ios-27 / GUEST SYSTEMS", 14, Color.rgb(113,205,194)));
        home.addView(text("Choose a session", 28, Color.WHITE));
        home.addView(text(Build.MODEL + " · " + Build.SUPPORTED_ABIS[0] + " · Android " + Build.VERSION.RELEASE, 13, Color.LTGRAY));
        home.addView(text("Darwin runtime check", 20, Color.WHITE));
        home.addView(text("Run the bundled ARM64 Mach-O workload in a supervised child process.", 16, Color.LTGRAY));
        openSession = new Button(this);
        openSession.setText("Start runtime session");
        openSession.setAllCaps(false);
        openSession.setOnClickListener(v -> {
            showSession(true);
            if (!sessionStarted) execute();
        });
        home.addView(openSession);
        home.addView(text("Apple system UI · Unavailable", 20, Color.WHITE));
        home.addView(text("SpringBoard needs Apple frameworks and system services that the current runtime cannot start.", 16, Color.LTGRAY));
        home.addView(text("Optional host setup", 20, Color.WHITE));
        home.addView(text("DirtyFrag is a separate app for temporary root on compatible Android builds. The runtime check does not require it.", 16, Color.LTGRAY));
        openDirtyFrag = new Button(this);
        openDirtyFrag.setAllCaps(false);
        openDirtyFrag.setOnClickListener(v -> {
            Intent launch = getPackageManager().getLaunchIntentForPackage(DIRTYFRAG_PACKAGE);
            if (launch != null) {
                try {
                    startActivity(launch);
                    return;
                } catch (ActivityNotFoundException ignored) {
                    // The companion may have been removed since the package lookup.
                }
            }
            updateDirtyFragButton();
        });
        home.addView(openDirtyFrag);

        session = new LinearLayout(this);
        session.setOrientation(LinearLayout.VERTICAL);
        root.addView(session, new LinearLayout.LayoutParams(-1, 0, 1));
        session.addView(text("ios-27 / RUNTIME SESSION", 14, Color.rgb(113,205,194)));
        status = text("Ready to test", 20, Color.WHITE);
        session.addView(status);
        run = new Button(this);
        run.setText("Run checks again");
        run.setAllCaps(false);
        session.addView(run);
        Button returnToPicker = new Button(this);
        returnToPicker.setText("Return to session picker");
        returnToPicker.setAllCaps(false);
        returnToPicker.setOnClickListener(v -> showSession(false));
        session.addView(returnToPicker);
        ScrollView scroll = new ScrollView(this);
        output = text("The test result will appear here.", 13, Color.rgb(196,219,231));
        output.setTypeface(Typeface.MONOSPACE); output.setTextIsSelectable(true); scroll.addView(output);
        session.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root); run.setOnClickListener(v -> execute());
        showSession(false);
        maybeRunFixture(getIntent());
    }
    private void showSession(boolean visible) {
        picker.setVisibility(visible ? View.GONE : View.VISIBLE);
        session.setVisibility(visible ? View.VISIBLE : View.GONE);
    }
    @Override protected void onResume() {
        super.onResume();
        updateDirtyFragButton();
    }
    private void updateDirtyFragButton() {
        boolean available = getPackageManager().getLaunchIntentForPackage(DIRTYFRAG_PACKAGE) != null;
        openDirtyFrag.setText(available ? "Open DirtyFrag" : "DirtyFrag app unavailable");
        openDirtyFrag.setEnabled(available);
    }
    @Override public void onBackPressed() {
        if (session.getVisibility() == View.VISIBLE) showSession(false);
        else super.onBackPressed();
    }
    private void maybeRunFixture(Intent intent) {
        if (BuildConfig.DEBUG && intent.getBooleanExtra("run_fixture", false) && run.isEnabled()) {
            showSession(true);
            execute();
        }
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent); setIntent(intent); maybeRunFixture(intent);
    }
    private byte[] asset(String name) throws IOException {
        try (InputStream in = getAssets().open(name); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] chunk = new byte[8192]; int n;
            while ((n = in.read(chunk)) != -1) {
                if (out.size() + n > 2097152) throw new IOException("Oversized bundled asset");
                out.write(chunk, 0, n);
            }
            return out.toByteArray();
        }
    }
    private void execute() {
        sessionStarted = true;
        run.setEnabled(false);
        openSession.setText("View running session");
        status.setText("Running native checks…");
        output.setText("");
        worker.execute(() -> {
            String report; boolean passed = false;
            try {
                byte[] fixture = asset("service-shim-executable");
                JSONObject manifest = new JSONObject(new String(asset("service-shim-symbols.json"), java.nio.charset.StandardCharsets.UTF_8));
                StringBuilder digest = new StringBuilder();
                for (byte b : MessageDigest.getInstance("SHA-256").digest(fixture)) digest.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
                if (!digest.toString().equals(manifest.getString("executable_sha256"))) throw new IOException("Bundled fixture hash mismatch");
                File image = new File(getFilesDir(), "service-shim-executable");
                try (FileOutputStream out = new FileOutputStream(image)) { out.write(fixture); }
                String libs = getApplicationInfo().nativeLibraryDir;
                Process process = new ProcessBuilder(libs + "/libios27_android_runner.so", image.getAbsolutePath(), libs + "/libios27_android_loader.so").redirectErrorStream(true).start();
                running = process;
                if (!process.waitFor(12, TimeUnit.SECONDS)) {
                    process.destroyForcibly(); throw new IOException("Native launcher exceeded 12-second deadline");
                }
                try (InputStream in = process.getInputStream(); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                    byte[] chunk = new byte[4096]; int n;
                    while ((n = in.read(chunk)) != -1) { if (out.size() + n > 65536) throw new IOException("Native output exceeded limit"); out.write(chunk, 0, n); }
                    report = "Fixture SHA-256 verified\n" + out.toString("UTF-8") + "\nLauncher exit: " + process.exitValue();
                }
                passed = process.exitValue() == 0 && report.contains("PASS: native ARM64 Mach-O returned 42");
            } catch (Exception e) { report = "Could not complete native execution: " + e; }
            finally { Process p = running; if (p != null) p.destroy(); running = null; }
            try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "last-result.txt"))) { out.write(report.getBytes(java.nio.charset.StandardCharsets.UTF_8)); } catch (IOException ignored) {}
            final String result = report; final boolean success = passed;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                output.setText(result);
                status.setText(success ? "Native workload passed" : "Execution not verified");
                run.setEnabled(true);
                openSession.setText("View runtime result");
            });
        });
    }
    @Override public void onDestroy() {
        Process p = running; if (p != null) p.destroyForcibly(); worker.shutdownNow(); super.onDestroy();
    }
}
