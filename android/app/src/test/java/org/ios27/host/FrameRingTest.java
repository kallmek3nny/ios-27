package org.ios27.host;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicReference;

/** Standalone JVM contracts, including an independent concurrent file writer. */
public final class FrameRingTest {
    private static int checks;
    private static void check(boolean value) {
        ++checks;
        if (!value) throw new AssertionError("Ring contract " + checks);
    }
    private static ByteBuffer bytes(int size) { return ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN); }
    private static byte[] fixture(long sequence, long producer) {
        ByteBuffer data = bytes(4096 * 5);
        data.putInt(0, 0x52424653).putInt(4, 2).putInt(8, 4).putInt(12, 4096);
        data.putLong(32, sequence).putInt(40, 1).putLong(264, producer);
        data.putLong(96, sequence).putLong(104, 9876543210L);
        data.putInt(112, 2).putInt(116, 2).putInt(120, 12);
        data.putInt(8192, 0xff112233).putInt(8196, 0x80445566).putInt(8200, 0xdddddddd);
        data.putInt(8204, 0xff030201).putInt(8208, 0x40302010).putInt(8212, 0xeeeeeeee);
        return data.array();
    }
    private static void invalid(Path path, byte[] content) throws IOException {
        Files.write(path, content);
        try { new FrameRing().read(path.toFile()); }
        catch (IOException expected) { ++checks; return; }
        throw new AssertionError("Invalid ring accepted");
    }
    private static void write(FileChannel channel, long offset, ByteBuffer data) throws IOException {
        while (data.hasRemaining()) {
            int n = channel.write(data, offset);
            if (n <= 0) throw new IOException("Short test write");
            offset += n;
        }
    }
    private static void stress(Path path) throws Exception {
        int slotBytes = 16384, frames = 1000;
        byte[] initial = Arrays.copyOf(fixture(0, 123), 4096 + 4 * slotBytes);
        ByteBuffer.wrap(initial).order(ByteOrder.LITTLE_ENDIAN).putInt(12, slotBytes);
        Files.write(path, initial);
        AtomicReference<Throwable> failed = new AtomicReference<>();
        Thread producer = new Thread(() -> {
            try (RandomAccessFile file = new RandomAccessFile(path.toFile(), "rw")) {
                FileChannel channel = file.getChannel();
                for (int seq = 1; seq <= frames; ++seq) {
                    int slot = seq % 4, meta = 64 + slot * 32;
                    write(channel, meta, bytes(8)); // invalidate before touching pixels
                    ByteBuffer pixels = bytes(slotBytes);
                    for (int i = 0; i < slotBytes / 4; ++i) pixels.putInt(0xff000000 | seq);
                    pixels.flip();
                    write(channel, 4096L + slot * slotBytes, pixels);
                    ByteBuffer dimensions = bytes(24);
                    dimensions.putLong(seq).putInt(64).putInt(64).putInt(256).putInt(0).flip();
                    write(channel, meta + 8, dimensions);
                    write(channel, meta, bytes(8).putLong(0, seq));
                    write(channel, 40, bytes(4).putInt(0, slot));
                    write(channel, 32, bytes(8).putLong(0, seq));
                    Thread.yield();
                }
            } catch (Throwable error) { failed.set(error); }
        }, "synthetic-ring-producer");
        FrameRing reader = new FrameRing();
        long newest = 0;
        int accepted = 0;
        producer.start();
        do {
            FrameFile.Frame frame = reader.read(path.toFile());
            if (frame != null) {
                int color = 0xff000000 | (int) frame.sequence;
                for (int pixel : frame.argb) check(pixel == color);
                newest = frame.sequence;
                ++accepted;
            }
        } while (producer.isAlive());
        producer.join();
        if (failed.get() != null) throw new AssertionError(failed.get());
        FrameFile.Frame last = reader.read(path.toFile());
        if (last != null) {
            for (int pixel : last.argb) check(pixel == (0xff000000 | frames));
            newest = last.sequence;
            ++accepted;
        }
        check(accepted > 0 && newest == frames);
        System.out.println("Concurrent ring writer: " + frames + " publications, " + accepted + " coherent frames read");
    }
    public static void main(String[] args) throws Exception {
        Path directory = Files.createTempDirectory("ios27-ring-test-");
        Path path = directory.resolve("framebuffer.ring");
        try {
            Files.write(path, fixture(0x100000001L, 7));
            FrameRing reader = new FrameRing();
            FrameFile.Frame frame = reader.read(path.toFile());
            check(frame != null && frame.width == 2 && frame.height == 2 && frame.sequence == 0x100000001L);
            check(Arrays.equals(frame.argb, new int[]{0xff112233, 0x80445566, 0xff030201, 0x40302010}));
            check(reader.read(path.toFile()) == null);
            Path replacement = directory.resolve("new-ring");
            Files.write(replacement, fixture(0x100000001L, 8));
            Files.move(replacement, path, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            check(reader.read(path.toFile()) != null); // same sequence, new producer/inode
            reader.reset();
            check(reader.read(path.toFile()) != null);
            Files.write(path, fixture(0, 9));
            check(reader.read(path.toFile()) == null);
            byte[] pending = fixture(1, 9);
            ByteBuffer.wrap(pending).order(ByteOrder.LITTLE_ENDIAN).putLong(96, 0);
            Files.write(path, pending);
            check(reader.read(path.toFile()) == null);

            int[][] bad = {{0, 0}, {4, 3}, {8, 0}, {8, 5}, {12, 0}, {12, 4095},
                    {12, 8192}, {12, Integer.MAX_VALUE}, {40, 4}, {40, -1}, {112, 0},
                    {116, 8193}, {120, 4}, {120, 9}, {120, Integer.MAX_VALUE}, {116, 1000}};
            for (int[] change : bad) {
                byte[] data = fixture(1, 9);
                ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN).putInt(change[0], change[1]);
                invalid(path, data);
            }
            invalid(path, new byte[20]);
            invalid(path, Arrays.copyOf(fixture(1, 9), 4096 * 5 - 1));
            invalid(path, Arrays.copyOf(fixture(1, 9), 4096 * 5 + 1));
            byte[] version1 = fixture(1, 9);
            ByteBuffer.wrap(version1).order(ByteOrder.LITTLE_ENDIAN).putInt(4, 1);
            Files.write(path, version1);
            check(new FrameRing().read(path.toFile()) != null);
            stress(path);
            System.out.println("Native frame ring: " + checks + " assertions passed");
        } finally {
            Files.deleteIfExists(path);
            Files.deleteIfExists(directory.resolve("new-ring"));
            Files.delete(directory);
        }
    }
}
