package dev.ios27.host;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PointF;
import android.os.Bundle;
import android.os.BatteryManager;
import android.os.Debug;
import android.os.Process;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.Choreographer;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/** Diagnostic Android shell. No Apple UIKit, SpringBoard or guest app execution. */
public final class MainActivity extends Activity implements SurfaceHolder.Callback, Choreographer.FrameCallback {
    static { System.loadLibrary("phase26e_jni"); }

    private static native long nativeCreate();
    private static native void nativeDestroy(long handle);
    private static native int nativeLifecycle(long handle, int action);
    private static native int nativeSurface(long handle, boolean exists, int width, int height, int rotation);
    private static native int nativePointer(long handle, int kind, int pointer, float x, float y);
    private static native int nativeKey(long handle, boolean down, int code);
    private static native int nativeText(long handle, String value, boolean composing);
    private static native void nativeFocus(long handle, boolean focused);
    private static native int nativeAccessibility(long handle);
    private static native long nativeGeneration(long handle);
    private static native int nativePresent(long handle, long generation);
    private static native int nativeRequestFrame(long handle);
    private static native long[] nativeMetrics(long handle);

    private long guest;
    private boolean resumed;
    private boolean surfaceReady;
    private boolean frameScheduled;
    private DiagnosticSurface display;
    private final SparseArray<PointF> touches = new SparseArray<>();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private long startWallMs;
    private long startCpuMs;
    private long peakDrawMicroseconds;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        startWallMs = SystemClock.elapsedRealtime();
        startCpuMs = Process.getElapsedCpuTime();
        guest = nativeCreate();
        if (guest == 0) { finish(); return; }
        display = new DiagnosticSurface(this);
        display.getHolder().addCallback(this);
        display.setFocusable(true);
        display.setFocusableInTouchMode(true);
        setContentView(display);
    }
    @Override protected void onResume() {
        super.onResume();
        if (guest == 0) return;
        resumed = true;
        nativeLifecycle(guest, 1); // initial launch is already foreground
        scheduleFrame();
    }
    @Override protected void onPause() {
        resumed = false;
        if (guest != 0) {
            cancelTouches();
            nativeLifecycle(guest, 0);
        }
        super.onPause();
    }
    @Override protected void onDestroy() {
        frameScheduled = false;
        Choreographer.getInstance().removeFrameCallback(this);
        if (guest != 0) {
            cancelTouches();
            nativeLifecycle(guest, 2);
            nativeDestroy(guest);
            guest = 0;
        }
        super.onDestroy();
    }
    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        if (guest != 0 && surfaceReady) {
            SurfaceHolder holder = display.getHolder();
            nativeSurface(guest, true, holder.getSurfaceFrame().width(),
                holder.getSurfaceFrame().height(), rotation());
            cancelTouches();
            scheduleFrame();
        }
    }
    private int rotation() {
        int raw = getWindowManager().getDefaultDisplay().getRotation();
        return raw == 1 ? 90 : raw == 2 ? 180 : raw == 3 ? 270 : 0;
    }
    @Override public void surfaceCreated(SurfaceHolder holder) { /* surfaceChanged supplies dimensions */ }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (guest == 0 || width <= 0 || height <= 0) return;
        cancelTouches();
        surfaceReady = nativeSurface(guest, true, width, height, rotation()) == 0;
        scheduleFrame();
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        if (guest != 0) {
            cancelTouches();
            nativeSurface(guest, false, 0, 0, 0);
        }
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (guest == 0) return;
        if (!focused) cancelTouches();
        nativeFocus(guest, focused);
        scheduleFrame();
    }
    private void scheduleFrame() {
        if (!frameScheduled && resumed && surfaceReady && guest != 0) {
            frameScheduled = true;
            Choreographer.getInstance().postFrameCallback(this);
        }
    }
    @Override public void doFrame(long frameTimeNanos) {
        frameScheduled = false;
        if (!resumed || !surfaceReady || guest == 0) return;
        nativeRequestFrame(guest);
        long generation = nativeGeneration(guest);
        long drawingStartNs = System.nanoTime();
        Canvas canvas = null;
        boolean posted = false;
        try {
            canvas = display.getHolder().lockCanvas();
            if (canvas != null) {
                canvas.drawColor(Color.rgb(22, 26, 38));
                paint.setColor(Color.WHITE); paint.setTextSize(30f);
                canvas.drawText("ios27 host diagnostic", 24f, 54f, paint);
                paint.setTextSize(20f);
                canvas.drawText("not UIKit or iOS app execution", 24f, 92f, paint);
                long[] m = nativeMetrics(guest);
                if (m != null && m.length == 7) {
                    canvas.drawText("events: " + m[0] + "  cancelled: " + m[2], 24f, 130f, paint);
                    canvas.drawText("frames: " + m[3] + "  surfaces: " + m[4], 24f, 162f, paint);
                    canvas.drawText("lifecycle: " + m[5] + "  log overflow: " + m[6], 24f, 194f, paint);
                }
                long uptimeMs = SystemClock.elapsedRealtime() - startWallMs;
                long cpuMs = Process.getElapsedCpuTime() - startCpuMs;
                long heapKiB = Debug.getNativeHeapAllocatedSize() / 1024L;
                canvas.drawText("cpu: " + cpuMs + "ms / " + uptimeMs + "ms", 24f, 226f, paint);
                canvas.drawText("native heap: " + heapKiB + " kib", 24f, 258f, paint);
                BatteryManager battery = (BatteryManager)getSystemService(BATTERY_SERVICE);
                int capacity = battery == null ? -1 : battery.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
                canvas.drawText("battery: " + (capacity < 0 ? "n/a" : capacity + "%"), 24f, 290f, paint);
                canvas.drawText("peak draw: " + peakDrawMicroseconds + " us", 24f, 322f, paint);
            }
        } catch (IllegalArgumentException | IllegalStateException ignored) {
            // A surface can disappear during rotation; never retain that canvas.
        } finally {
            if (canvas != null) {
                try {
                    display.getHolder().unlockCanvasAndPost(canvas);
                    posted = true;
                } catch (IllegalArgumentException | IllegalStateException ignored) {
                    // The surface may retire between lock and post during rotation.
                }
            }
        }
        if (posted && guest != 0) {
            nativePresent(guest, generation);
            long drawUs = (System.nanoTime() - drawingStartNs) / 1000L;
            peakDrawMicroseconds = Math.max(peakDrawMicroseconds, drawUs);
        }
        // On-demand only: do not wake a 120 Hz panel continuously when idle.
    }
    private void cancelTouches() {
        if (guest != 0) for (int i = 0; i < touches.size(); ++i) {
            int id = touches.keyAt(i);
            PointF pos = touches.valueAt(i);
            nativePointer(guest, 4, id, pos.x, pos.y);
        }
        touches.clear();
    }
    private boolean route(MotionEvent event) {
        if (guest == 0) return false;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) { cancelTouches(); return true; }
        if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); ++i) {
                int id = event.getPointerId(i);
                float x = event.getX(i), y = event.getY(i);
                if (nativePointer(guest, 2, id, x, y) == 0)
                    touches.put(id, new PointF(x, y));
            }
            return true;
        }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN ||
            action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            int i = event.getActionIndex();
            int id = event.getPointerId(i);
            float x = event.getX(i), y = event.getY(i);
            int kind = (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) ? 1 : 3;
            int status = nativePointer(guest, kind, id, x, y);
            if (status != 0) return false;
            if (kind == 1) touches.put(id, new PointF(x, y)); else touches.remove(id);
            return true;
        }
        return false;
    }
    @Override public boolean onKeyDown(int code, KeyEvent event) {
        if (guest != 0) { nativeKey(guest, true, code); scheduleFrame(); }
        return super.onKeyDown(code, event);
    }
    @Override public boolean onKeyUp(int code, KeyEvent event) {
        if (guest != 0) { nativeKey(guest, false, code); scheduleFrame(); }
        return super.onKeyUp(code, event);
    }
    private final class DiagnosticSurface extends SurfaceView {
        DiagnosticSurface(Context context) { super(context); }
        @Override public boolean onTouchEvent(MotionEvent event) {
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                requestFocus();
                InputMethodManager imm = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
                if (imm != null) imm.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
                if (nativeAccessibility(guest) == 0)
                    sendAccessibilityEvent(AccessibilityEvent.TYPE_VIEW_FOCUSED);
            }
            boolean routed = route(event);
            scheduleFrame();
            return routed;
        }
        @Override public boolean onCheckIsTextEditor() { return true; }
        @Override public InputConnection onCreateInputConnection(EditorInfo attrs) {
            attrs.inputType = android.text.InputType.TYPE_CLASS_TEXT;
            attrs.imeOptions = EditorInfo.IME_ACTION_DONE;
            return new BaseInputConnection(this, false) {
                @Override public boolean commitText(CharSequence text, int cursor) {
                    if (guest == 0 || text == null) return false;
                    boolean accepted = nativeText(guest, text.toString(), false) == 0;
                    if (accepted) scheduleFrame();
                    return accepted;
                }
                @Override public boolean setComposingText(CharSequence text, int cursor) {
                    if (guest == 0 || text == null) return false;
                    boolean accepted = nativeText(guest, text.toString(), true) == 0;
                    if (accepted) scheduleFrame();
                    return accepted;
                }
                @Override public boolean finishComposingText() {
                    if (guest == 0) return false;
                    boolean accepted = nativeText(guest, "", true) == 0;
                    if (accepted) scheduleFrame();
                    return accepted;
                }
            };
        }
    }
}
