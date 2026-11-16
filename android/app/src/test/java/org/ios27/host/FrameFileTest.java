package org.ios27.host;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.Arrays;

/** Standalone JVM contract test; does not need an Android activity or device. */
public final class FrameFileTest {
    private static byte[] frame(int width, int height, int stride, byte[] pixels) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        for (int word : new int[]{width, height, stride, -1}) out.writeInt(Integer.reverseBytes(word));
        out.write(pixels);
        return bytes.toByteArray();
    }
    private static void invalid(byte[] bytes) throws IOException {
        try { FrameFile.read(new ByteArrayInputStream(bytes)); }
        catch (IOException expected) { return; }
        throw new AssertionError("Invalid frame was accepted");
    }
    public static void main(String[] args) throws Exception {
        byte[] pixels = {0x33, 0x22, 0x11, -1, 0x66, 0x55, 0x44, -128, 9, 9, 9, 9,
                         0x01, 0x02, 0x03, -1, 0x10, 0x20, 0x30, 0x40, 8, 8, 8, 8};
        byte[] valid = frame(2, 2, 12, pixels);
        FrameFile.Frame decoded = FrameFile.read(new ByteArrayInputStream(valid));
        if (decoded.width != 2 || decoded.height != 2 || decoded.sequence != 0xffffffffL ||
                !Arrays.equals(decoded.argb, new int[]{0xff112233, 0x80445566, 0xff030201, 0x40302010})) {
            throw new AssertionError("BGRA color, alpha, row padding or sequence changed");
        }
        invalid(new byte[15]);
        invalid(Arrays.copyOf(valid, valid.length - 1));
        invalid(Arrays.copyOf(valid, valid.length + 1));
        invalid(frame(0, 1, 4, new byte[4]));
        invalid(frame(2, 1, 4, new byte[4]));
        invalid(frame(1, 1, 5, new byte[5]));
        invalid(frame(-1, 2, 4, new byte[0]));
        invalid(frame(8192, 8192, 32768, new byte[0]));
        invalid(frame(1, 1, Integer.MAX_VALUE, new byte[0]));
        System.out.println("Native frame decoder: 10 contracts passed");
    }
}
