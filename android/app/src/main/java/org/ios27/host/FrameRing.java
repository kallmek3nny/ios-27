package org.ios27.host;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;
import java.nio.channels.FileChannel;

/** Reader for the pinned SimFramebuffer SFBR v1/v2 protocol.
 * Uses positional file reads and checks publication again after copying; it
 * never presents an in-progress/overwritten slot or maps a truncatable file.
 * Single consumer: returned pixels are reusable, so consume before read(). */
final class FrameRing {
    private static final int HEADER_SIZE = 4096, SLOTS = 4, METADATA_SIZE = 272;
    private final ByteBuffer before = ByteBuffer.allocate(METADATA_SIZE).order(ByteOrder.LITTLE_ENDIAN);
    private final ByteBuffer after = ByteBuffer.allocate(METADATA_SIZE).order(ByteOrder.LITTLE_ENDIAN);
    private ByteBuffer pixels;
    private int[] argb;
    private long lastSequence, lastProducer;

    void reset() { lastSequence = 0; lastProducer = 0; }

    private static void readFully(FileChannel file, ByteBuffer target, long offset) throws IOException {
        while (target.hasRemaining()) {
            int read = file.read(target, offset);
            if (read <= 0) throw new IOException("Truncated native frame ring");
            offset += read;
        }
        target.flip();
    }

    FrameFile.Frame read(File path) throws IOException {
        // A new descriptor each sample also follows the producer's atomic
        // file replacement on restart, including when its sequence resets.
        try (FileInputStream input = new FileInputStream(path); FileChannel file = input.getChannel()) {
            before.clear();
            readFully(file, before, 0);
            int version = before.getInt(4), slotBytes = before.getInt(12);
            if (before.getInt(0) != 0x52424653 || (version != 1 && version != 2) ||
                    before.getInt(8) != SLOTS || slotBytes <= 0 || slotBytes > 134217728 ||
                    (slotBytes & 4095) != 0 || file.size() != HEADER_SIZE + (long) SLOTS * slotBytes) {
                throw new IOException("Invalid native frame ring layout");
            }
            long sequence = before.getLong(32), producer = before.getLong(264);
            int latest = before.getInt(40);
            if (sequence == 0) return null;
            if (latest < 0 || latest >= SLOTS) throw new IOException("Invalid native frame ring slot");
            int slot = 64 + latest * 32;
            if (before.getLong(slot) != sequence) return null;
            if (sequence == lastSequence && producer == lastProducer) return null;
            int width = before.getInt(slot + 16), height = before.getInt(slot + 20);
            int stride = before.getInt(slot + 24);
            FrameFile.validate(width, height, stride);
            int length = stride * height;
            if (length > slotBytes) throw new IOException("Native frame exceeds its ring slot");
            if (pixels == null || pixels.capacity() != length) {
                pixels = ByteBuffer.allocate(length).order(ByteOrder.LITTLE_ENDIAN);
            }
            pixels.clear();
            readFully(file, pixels, HEADER_SIZE + (long) latest * slotBytes);
            after.clear();
            readFully(file, after, 0);
            // The header must still advertise this frame. Inactive slots may
            // be zero-filled by MADV_REMOVE without changing their slot seq.
            if (after.getLong(32) != sequence || after.getInt(40) != latest ||
                    after.getLong(264) != producer || after.getInt(0) != before.getInt(0) ||
                    after.getInt(4) != version || after.getInt(8) != SLOTS || after.getInt(12) != slotBytes) return null;
            for (int offset = 0; offset < 32; offset += 8) {
                if (after.getLong(slot + offset) != before.getLong(slot + offset)) return null;
            }
            if (argb == null || argb.length != width * height) argb = new int[width * height];
            IntBuffer words = pixels.asIntBuffer();
            for (int y = 0; y < height; ++y) {
                words.position(y * (stride / 4));
                words.get(argb, y * width, width);
            }
            lastSequence = sequence; lastProducer = producer;
            return new FrameFile.Frame(width, height, sequence, argb);
        }
    }
}
