package org.ios27.host;

import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;

/** SimFramebuffer's atomic snapshot: four little-endian uint32s, then BGRA rows. */
final class FrameFile {
    static final class Frame {
        final int width, height;
        final long sequence;
        final int[] argb;
        Frame(int width, int height, long sequence, int[] argb) {
            this.width = width; this.height = height; this.sequence = sequence; this.argb = argb;
        }
    }

    static Frame read(InputStream stream) throws IOException {
        DataInputStream input = new DataInputStream(stream);
        int width = Integer.reverseBytes(input.readInt());
        int height = Integer.reverseBytes(input.readInt());
        int stride = Integer.reverseBytes(input.readInt());
        long sequence = Integer.toUnsignedLong(Integer.reverseBytes(input.readInt()));
        validate(width, height, stride);
        int[] pixels = new int[width * height];
        byte[] row = new byte[stride];
        for (int y = 0; y < height; ++y) {
            input.readFully(row);
            for (int x = 0; x < width; ++x) {
                int p = x * 4;
                pixels[y * width + x] = (row[p] & 255) | ((row[p + 1] & 255) << 8) |
                        ((row[p + 2] & 255) << 16) | ((row[p + 3] & 255) << 24);
            }
        }
        if (input.read() != -1) throw new IOException("Native frame has trailing bytes");
        return new Frame(width, height, sequence, pixels);
    }

    static void validate(int width, int height, int stride) throws IOException {
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
                (long) width * height > 16777216 || stride < width * 4 ||
                stride > 32768 || (stride & 3) != 0 || (long) stride * height > 134217728) {
            throw new IOException("Invalid native frame dimensions");
        }
    }
}
