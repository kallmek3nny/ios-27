package org.ios27.host;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** Debug preview of frames actually submitted to the native simulator bridge. */
public final class NativeFrameActivity extends Activity {
    private final ScheduledExecutorService reader = Executors.newSingleThreadScheduledExecutor();
    private final ThreadPoolExecutor input = new ThreadPoolExecutor(1, 1, 0, TimeUnit.MILLISECONDS,
            new ArrayBlockingQueue<>(64), new ThreadPoolExecutor.DiscardOldestPolicy());
    private ScheduledFuture<?> sampling;
    private volatile boolean active;
    private volatile LocalSocket socket;
    private volatile int inputEpoch;
    private File frameFile, ringFile, socketFile;
    private final FrameRing ring = new FrameRing();
    private final AtomicBoolean deliveryPending = new AtomicBoolean();
    private int readerEpoch = -1;
    private long lastFallbackCheck;
    private ImageView image;
    private TextView status;
    private Bitmap bitmap;
    private int frameWidth, frameHeight, pointer = -1;
    private float touchX, touchY;
    private String edge = "none";
    private volatile String lastMessage = "";
    private volatile long lastModified = -1;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (!BuildConfig.DEBUG) { finish(); return; }
        String device = getIntent().getStringExtra("device");
        if (device == null) device = "ios27-device-min";
        if (!device.matches("[A-Za-z0-9_-]{1,64}")) { finish(); return; }
        File data = new File(new File(getFilesDir(), device), "data");
        frameFile = new File(data, "framebuffer.bgra");
        ringFile = new File(data, "framebuffer.ring");
        socketFile = new File(data, "simhid.sock");
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        root.setFitsSystemWindows(true);
        image = new ImageView(this);
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        root.addView(image, new FrameLayout.LayoutParams(-1, -1));
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(0xb0000000);
        status.setTextSize(13);
        int pad = Math.round(10 * getResources().getDisplayMetrics().density);
        status.setPadding(pad, pad, pad, pad);
        status.setText("Waiting for a native simulator frame…");
        root.addView(status, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP));
        setContentView(root);
        image.setOnTouchListener((view, event) -> touch(event));
    }

    @Override protected void onStart() {
        super.onStart();
        if (frameFile == null) return;
        ++inputEpoch;
        active = true;
        lastModified = -1;
        sampling = reader.scheduleWithFixedDelay(this::loadFrame, 0, 33, TimeUnit.MILLISECONDS);
    }

    private void message(String value) {
        if (value.equals(lastMessage)) return;
        lastMessage = value;
        runOnUiThread(() -> { if (active && !isDestroyed()) status.setText(value); });
    }

    private void loadFrame() {
        if (!active || !deliveryPending.compareAndSet(false, true)) return;
        boolean queued = false;
        int epoch = inputEpoch;
        try {
            if (readerEpoch != epoch) {
                readerEpoch = epoch; ring.reset(); lastFallbackCheck = 0;
            }
            boolean live = ringFile.isFile();
            FrameFile.Frame frame;
            if (live) {
                frame = ring.read(ringFile);
                if (frame == null) return;
            } else {
                long now = System.nanoTime();
                if (now - lastFallbackCheck < TimeUnit.MILLISECONDS.toNanos(250)) return;
                lastFallbackCheck = now;
                long modified = frameFile.lastModified();
                if (modified == 0 || modified == lastModified) return;
                try (BufferedInputStream stream = new BufferedInputStream(new FileInputStream(frameFile))) {
                    frame = FrameFile.read(stream);
                }
                lastModified = modified;
            }
            runOnUiThread(() -> {
                try {
                    if (!active || epoch != inputEpoch || isDestroyed()) return;
                    if (bitmap == null || bitmap.getWidth() != frame.width || bitmap.getHeight() != frame.height) {
                        bitmap = Bitmap.createBitmap(frame.width, frame.height, Bitmap.Config.ARGB_8888);
                        image.setImageBitmap(bitmap);
                    }
                    bitmap.setPixels(frame.argb, 0, frame.width, 0, 0, frame.width, frame.height);
                    image.invalidate();
                    frameWidth = frame.width; frameHeight = frame.height;
                    lastMessage = "Native " + (live ? "ring" : "snapshot") + " frame " +
                            Long.toUnsignedString(frame.sequence) + " · " + frame.width + " × " + frame.height;
                    status.setText(lastMessage);
                } finally {
                    deliveryPending.set(false);
                }
            });
            queued = true;
        } catch (IOException | RuntimeException error) {
            message("Frame preview: " + error.getMessage());
        } finally {
            if (!queued) deliveryPending.set(false);
        }
    }

    private boolean touch(MotionEvent event) {
        if (!active || frameWidth == 0 || image.getWidth() == 0 || image.getHeight() == 0) return false;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) pointer = event.getPointerId(0);
        int index = event.findPointerIndex(pointer);
        if (pointer == -1 || index < 0) return false;
        float scale = Math.min((float) image.getWidth() / frameWidth, (float) image.getHeight() / frameHeight);
        float x = (event.getX(index) - (image.getWidth() - frameWidth * scale) / 2) / (frameWidth * scale);
        float y = (event.getY(index) - (image.getHeight() - frameHeight * scale) / 2) / (frameHeight * scale);
        if (action == MotionEvent.ACTION_DOWN && (x < 0 || x > 1 || y < 0 || y > 1)) {
            pointer = -1; return false;
        }
        touchX = Math.max(0, Math.min(1, x)); touchY = Math.max(0, Math.min(1, y));
        if (action == MotionEvent.ACTION_DOWN) {
            edge = touchY > .97f ? "bottom" : touchY < .03f ? "top" :
                    touchX < .03f ? "left" : touchX > .97f ? "right" : "none";
            sendTouch("down", touchX, touchY, edge);
        } else if (action == MotionEvent.ACTION_MOVE) {
            sendTouch("move", touchX, touchY, edge);
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL ||
                (action == MotionEvent.ACTION_POINTER_UP && event.getPointerId(event.getActionIndex()) == pointer)) {
            sendTouch("up", touchX, touchY, edge); pointer = -1;
        }
        return true;
    }

    private void sendTouch(String phase, float x, float y, String fromEdge) {
        String command = String.format(Locale.ROOT, "touch %s %.6f %.6f %s\n", phase, x, y, fromEdge);
        int epoch = inputEpoch;
        try {
            input.execute(() -> {
                if (!active || epoch != inputEpoch) return;
                try {
                    LocalSocket connected = socket;
                    if (connected == null) {
                        connected = new LocalSocket(LocalSocket.SOCKET_STREAM);
                        socket = connected;
                        if (!active || epoch != inputEpoch) { closeSocket(); return; }
                        connected.connect(new LocalSocketAddress(socketFile.getAbsolutePath(), LocalSocketAddress.Namespace.FILESYSTEM));
                        connected.getOutputStream().write("touch up 0 0 none\n".getBytes(StandardCharsets.US_ASCII));
                    }
                    if (!active || epoch != inputEpoch) { closeSocket(); return; }
                    connected.getOutputStream().write(command.getBytes(StandardCharsets.US_ASCII));
                } catch (IOException error) {
                    closeSocket();
                    message("Native frame preview · touch connection unavailable");
                }
            });
        } catch (RejectedExecutionException ignored) { }
    }

    private void closeSocket() {
        LocalSocket previous = socket; socket = null;
        if (previous != null) try { previous.close(); } catch (IOException ignored) { }
    }

    @Override protected void onStop() {
        active = false; pointer = -1;
        ++inputEpoch;
        if (sampling != null) sampling.cancel(false);
        input.getQueue().clear();
        closeSocket();
        super.onStop();
    }

    @Override public void onDestroy() {
        active = false;
        reader.shutdownNow(); input.shutdownNow(); closeSocket();
        super.onDestroy();
    }
}
