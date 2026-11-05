package dev.ios27.phase27e;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.InputType;
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
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Original demo on public Android APIs, not an Apple framework runtime. */
public final class MainActivity extends Activity implements SurfaceHolder.Callback, Choreographer.FrameCallback {
    static { System.loadLibrary("phase27e_host"); }
    private static native long nativeCreate();
    private static native void nativeDestroy(long handle);
    private static native int nativeLifecycle(long handle, int state);
    private static native long nativeSurface(long handle, boolean present, int w, int h, int rotation);
    private static native int nativePointer(long handle, int kind, int id, float x, float y, long uptime);
    private static native int nativeKey(long handle, boolean down, int key);
    private static native int nativeText(long handle, String text, boolean composing);
    private static native void nativeFocus(long handle, boolean focus);
    private static native int nativeAccessibility(long handle);
    private static native boolean nativeRender(long handle, long generation, ByteBuffer rgba,
                                               int w, int h, int stride, long uptime);
    private static native int nativePresented(long handle, long generation);
    private static native long[] nativeMetrics(long handle);

    private static final int MAX_PIXELS = 2097152;
    private long host;
    private long surfaceGeneration;
    private boolean resumed;
    private boolean ready;
    private boolean scheduled;
    private long animateUntil;
    private int surfaceWidth, surfaceHeight;
    private Bitmap bitmap;
    private ByteBuffer frameBuffer;
    private DemoSurface surface;
    private final SparseArray<float[]> pointers = new SparseArray<>();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private String latestText = "";

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        host = nativeCreate();
        if (host == 0) { finish(); return; }
        surface = new DemoSurface(this);
        surface.getHolder().addCallback(this);
        surface.setFocusable(true);
        surface.setFocusableInTouchMode(true);
        setContentView(surface);
    }
    @Override protected void onResume() {
        super.onResume();
        if (host == 0) return;
        resumed = nativeLifecycle(host, 1) == 0;
        scheduleFrame();
    }
    @Override protected void onPause() {
        resumed = false;
        Choreographer.getInstance().removeFrameCallback(this);
        scheduled = false;
        cancelPointers();
        if (host != 0) nativeLifecycle(host, 0);
        super.onPause();
    }
    @Override protected void onDestroy() {
        resumed = false;
        Choreographer.getInstance().removeFrameCallback(this);
        scheduled = false;
        cancelPointers();
        if (host != 0) {
            nativeLifecycle(host, 2);
            nativeDestroy(host);
            host = 0;
        }
        releaseFrame();
        super.onDestroy();
    }
    @Override public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (level >= TRIM_MEMORY_RUNNING_LOW && !resumed) releaseFrame();
    }
    @Override public void onConfigurationChanged(Configuration config) {
        super.onConfigurationChanged(config);
        if (host != 0 && ready) {
            cancelPointers();
            surfaceGeneration = nativeSurface(host, true, surfaceWidth, surfaceHeight, rotation());
            ready = surfaceGeneration != 0;
            scheduleFrame();
        }
    }
    private int rotation() {
        int v = getWindowManager().getDefaultDisplay().getRotation();
        return v == 1 ? 90 : v == 2 ? 180 : v == 3 ? 270 : 0;
    }
    @Override public void surfaceCreated(SurfaceHolder holder) { /* wait for surfaceChanged */ }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        if (host == 0 || w <= 0 || h <= 0 || w > 8192 || h > 8192) return;
        cancelPointers();
        releaseFrame();
        surfaceWidth = w;
        surfaceHeight = h;
        surfaceGeneration = nativeSurface(host, true, w, h, rotation());
        ready = surfaceGeneration != 0;
        if (ready) allocateFrame(w, h);
        scheduleFrame();
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        ready = false;
        Choreographer.getInstance().removeFrameCallback(this);
        scheduled = false;
        cancelPointers();
        if (host != 0) nativeSurface(host, false, 0, 0, 0);
        surfaceGeneration = 0;
        releaseFrame();
    }
    private void allocateFrame(int w, int h) {
        double scale = Math.min(1.0, Math.min(2048.0 / Math.max(w, h),
                Math.sqrt((double)MAX_PIXELS / ((double)w * h))));
        int bw = Math.max(1, (int)Math.floor(w * scale));
        int bh = Math.max(1, (int)Math.floor(h * scale));
        if ((long)bw * bh > MAX_PIXELS) return;
        try {
            bitmap = Bitmap.createBitmap(bw, bh, Bitmap.Config.ARGB_8888);
            frameBuffer = ByteBuffer.allocateDirect(bw * bh * 4).order(ByteOrder.nativeOrder());
        } catch (OutOfMemoryError error) {
            releaseFrame();
            android.util.Log.e("ios27-27e", "bounded frame allocation failed", error);
        }
    }
    private void releaseFrame() {
        frameBuffer = null;
        if (bitmap != null) { bitmap.recycle(); bitmap = null; }
    }
    private void scheduleFrame() {
        if (!scheduled && ready && resumed && host != 0 && bitmap != null) {
            scheduled = true;
            Choreographer.getInstance().postFrameCallback(this);
        }
    }
    @Override public void doFrame(long frameTimeNanos) {
        scheduled = false;
        if (!ready || !resumed || host == 0 || bitmap == null || frameBuffer == null) return;
        long handle = host, generation = surfaceGeneration;
        long now = SystemClock.uptimeMillis();
        int bw = bitmap.getWidth(), bh = bitmap.getHeight();
        if (!nativeRender(handle, generation, frameBuffer, bw, bh, bw * 4, now)) return;
        frameBuffer.position(0);
        bitmap.copyPixelsFromBuffer(frameBuffer);
        Canvas canvas = null;
        boolean posted = false;
        try {
            canvas = surface.getHolder().lockCanvas();
            if (canvas != null) {
                canvas.drawColor(Color.BLACK);
                paint.setColor(Color.WHITE);
                canvas.drawBitmap(bitmap, null, new Rect(0, 0, canvas.getWidth(), canvas.getHeight()), paint);
                drawHud(canvas);
            }
        } catch (IllegalStateException | IllegalArgumentException exception) {
            android.util.Log.w("ios27-27e", "surface was retired during drawing");
        } finally {
            if (canvas != null) {
                try { surface.getHolder().unlockCanvasAndPost(canvas); posted = true; }
                catch (IllegalStateException | IllegalArgumentException ignored) { /* retired surface */ }
            }
        }
        if (posted && host == handle && surfaceGeneration == generation)
            nativePresented(handle, generation);
        if (now < animateUntil) scheduleFrame();
    }
    private void drawHud(Canvas canvas) {
        float density = getResources().getDisplayMetrics().scaledDensity;
        float padding = 20f * density;
        paint.setColor(Color.WHITE);
        paint.setTextSize(27f * density);
        paint.setFakeBoldText(true);
        canvas.drawText("ios27 / native", padding, 58f * density, paint);
        paint.setFakeBoldText(false);
        paint.setTextSize(13f * density);
        canvas.drawText("phase 27e  •  original software scene", padding, 84f * density, paint);
        long[] metrics = nativeMetrics(host);
        if (metrics != null && metrics.length == 6) {
            canvas.drawText("events " + metrics[0] + "   frames " + metrics[1] +
                "   touches " + metrics[4], padding, canvas.getHeight() - 72f * density, paint);
            canvas.drawText("surface generations " + metrics[2] +
                "   cancels " + metrics[3], padding, canvas.getHeight() - 49f * density, paint);
        }
        canvas.drawText(latestText.isEmpty() ? "touch, drag, rotate, or type" : latestText,
            padding, canvas.getHeight() - 26f * density, paint);
    }
    private void cancelPointers() {
        if (host != 0) for (int i = 0; i < pointers.size(); ++i) {
            float[] xy = pointers.valueAt(i);
            nativePointer(host, 4, pointers.keyAt(i), xy[0], xy[1], SystemClock.uptimeMillis());
        }
        pointers.clear();
    }
    private boolean route(MotionEvent event) {
        if (host == 0 || !ready) return false;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) { cancelPointers(); return true; }
        if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); ++i) {
                int id = event.getPointerId(i);
                if (pointers.get(id) == null) continue;
                float x = event.getX(i), y = event.getY(i);
                if (nativePointer(host, 2, id, x, y, SystemClock.uptimeMillis()) == 0)
                    pointers.put(id, new float[]{x,y});
            }
            animateUntil = SystemClock.uptimeMillis() + 700;
            scheduleFrame();
            return true;
        }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN ||
            action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            int index = event.getActionIndex(), id = event.getPointerId(index);
            int kind = (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) ? 1 : 3;
            if (kind == 3 && pointers.get(id) == null) return false;
            float x = event.getX(index), y = event.getY(index);
            if (nativePointer(host, kind, id, x, y, SystemClock.uptimeMillis()) != 0) return false;
            if (kind == 1) pointers.put(id, new float[]{x,y}); else pointers.remove(id);
            animateUntil = SystemClock.uptimeMillis() + 700;
            scheduleFrame();
            return true;
        }
        return false;
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (host == 0) return;
        if (!focused) cancelPointers();
        nativeFocus(host, focused);
        scheduleFrame();
    }
    @Override public boolean onKeyDown(int code, KeyEvent event) {
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_VOLUME_UP ||
            code == KeyEvent.KEYCODE_VOLUME_DOWN) return super.onKeyDown(code, event);
        if (host != 0 && nativeKey(host, true, code) == 0) { scheduleFrame(); return true; }
        return super.onKeyDown(code, event);
    }
    @Override public boolean onKeyUp(int code, KeyEvent event) {
        if (host != 0 && nativeKey(host, false, code) == 0) { scheduleFrame(); return true; }
        return super.onKeyUp(code, event);
    }
    private final class DemoSurface extends SurfaceView {
        DemoSurface(Context context) { super(context); }
        @Override public boolean onTouchEvent(MotionEvent event) {
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                requestFocus();
                if (host != 0 && nativeAccessibility(host) == 0)
                    sendAccessibilityEvent(AccessibilityEvent.TYPE_VIEW_FOCUSED);
                InputMethodManager keyboard = (InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
                if (keyboard != null) keyboard.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
            }
            return route(event);
        }
        @Override public boolean onCheckIsTextEditor() { return true; }
        @Override public InputConnection onCreateInputConnection(EditorInfo info) {
            info.inputType = InputType.TYPE_CLASS_TEXT;
            info.imeOptions = EditorInfo.IME_ACTION_DONE;
            return new BaseInputConnection(this, false) {
                @Override public boolean commitText(CharSequence value, int position) {
                    return sendText(value, false);
                }
                @Override public boolean setComposingText(CharSequence value, int position) {
                    return sendText(value, true);
                }
                @Override public boolean finishComposingText() { return sendText("", true); }
                private boolean sendText(CharSequence value, boolean compose) {
                    if (host == 0 || value == null || value.length() > 127) return false;
                    String text = value.toString();
                    if (nativeText(host, text, compose) != 0) return false;
                    if (!compose) latestText = text.length() > 36 ? text.substring(0,36) : text;
                    scheduleFrame();
                    return true;
                }
            };
        }
    }
}