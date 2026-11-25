package org.ios27.host;

import android.app.Activity;
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
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private volatile Process running;
    private TextView status, output;
    private Button run;
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
        root.addView(text("ios-27 / RUNTIME", 14, Color.rgb(113,205,194)));
        root.addView(text("Native execution host", 28, Color.WHITE));
        root.addView(text("An Android foundation for Darwin runtime research. Apple’s system UI and frameworks are not running yet.", 16, Color.LTGRAY));
        root.addView(text(Build.MODEL + " · " + Build.SUPPORTED_ABIS[0] + " · Android " + Build.VERSION.RELEASE, 13, Color.LTGRAY));
        status = text("Ready to test", 20, Color.WHITE); root.addView(status);
        run = new Button(this); run.setText("Run bundled ARM64 workload"); run.setAllCaps(false); root.addView(run);
        if (BuildConfig.DEBUG) {
            Button display = new Button(this); display.setText("Open native frame preview"); display.setAllCaps(false);
            display.setOnClickListener(v -> startActivity(new Intent(this, NativeFrameActivity.class)));
            root.addView(display);
        }
        root.addView(text("Runs only our original bundled test program in a supervised child process. Checks virtual files, memory mappings and heap allocation. No Apple binaries or network access.", 14, Color.LTGRAY));
        ScrollView scroll = new ScrollView(this);
        output = text("The test result will appear here.", 13, Color.rgb(196,219,231));
        output.setTypeface(Typeface.MONOSPACE); output.setTextIsSelectable(true); scroll.addView(output);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root); run.setOnClickListener(v -> execute("success"));
        maybeRunFixture(getIntent());
    }
    // Debug-only supervised modes and their PASS prefixes; keep in sync with runner_modes.h.
    private static final String[][] MODES = {
        {"success", "PASS: native ARM64 Mach-O returned 42."},
        {"trap", "PASS: supervised trap mode: signaled by SIGTRAP."},
        {"deadline", "PASS: supervised deadline mode: timed out and killed by SIGKILL."},
        {"raw-syscall", "PASS: supervised raw-syscall mode: signaled by SIGSYS."}
    };
    private static String expectedPass(String mode) {
        for (String[] m : MODES) if (m[0].equals(mode)) return m[1];
        return null;
    }
    private void maybeRunFixture(Intent intent) {
        if (!BuildConfig.DEBUG || !intent.getBooleanExtra("run_fixture", false) || !run.isEnabled()) return;
        String mode = intent.getStringExtra("fixture_mode");
        execute(mode == null ? "success" : mode);
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
    private void execute(String mode) {
        run.setEnabled(false); status.setText("Running native checks…"); output.setText("");
        final String pass = expectedPass(mode);
        worker.execute(() -> {
            String report; boolean passed = false;
            try {
                if (pass == null) throw new IOException("Unknown supervised mode: " + mode);
                byte[] fixture = asset("service-shim-executable");
                JSONObject manifest = new JSONObject(new String(asset("service-shim-symbols.json"), java.nio.charset.StandardCharsets.UTF_8));
                StringBuilder digest = new StringBuilder();
                for (byte b : MessageDigest.getInstance("SHA-256").digest(fixture)) digest.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
                if (!digest.toString().equals(manifest.getString("executable_sha256"))) throw new IOException("Bundled fixture hash mismatch");
                File image = new File(getFilesDir(), "service-shim-executable");
                try (FileOutputStream out = new FileOutputStream(image)) { out.write(fixture); }
                String libs = getApplicationInfo().nativeLibraryDir;
                Process process = new ProcessBuilder(libs + "/libios27_android_runner.so", image.getAbsolutePath(), libs + "/libios27_android_loader.so", mode).redirectErrorStream(true).start();
                running = process;
                if (!process.waitFor(12, TimeUnit.SECONDS)) {
                    process.destroyForcibly(); throw new IOException("Native launcher exceeded 12-second deadline");
                }
                try (InputStream in = process.getInputStream(); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                    byte[] chunk = new byte[4096]; int n;
                    while ((n = in.read(chunk)) != -1) { if (out.size() + n > 65536) throw new IOException("Native output exceeded limit"); out.write(chunk, 0, n); }
                    report = "Fixture SHA-256 verified\n" + out.toString("UTF-8") + "\nLauncher exit: " + process.exitValue();
                }
                passed = process.exitValue() == 0 && report.contains(pass);
            } catch (Exception e) { report = "Could not complete native execution: " + e; }
            finally { Process p = running; if (p != null) p.destroy(); running = null; }
            try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "last-result.txt"))) { out.write(report.getBytes(java.nio.charset.StandardCharsets.UTF_8)); } catch (IOException ignored) {}
            final String result = report; final boolean success = passed;
            runOnUiThread(() -> { if (!isDestroyed()) { output.setText(result); status.setText(success ? "Native workload passed" : "Execution not verified"); run.setEnabled(true); } });
        });
    }
    @Override public void onDestroy() {
        Process p = running; if (p != null) p.destroyForcibly(); worker.shutdownNow(); super.onDestroy();
    }
}
