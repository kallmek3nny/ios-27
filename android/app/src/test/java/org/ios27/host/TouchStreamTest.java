package org.ios27.host;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/** JVM transport contracts: slow peers, reconnects, lifecycle and gesture order. */
public final class TouchStreamTest {
    private static int checks;
    private static void check(boolean condition, String what) {
        ++checks;
        if (!condition) throw new AssertionError(what);
    }
    private static void await(CountDownLatch latch) throws IOException {
        try {
            if (!latch.await(5, TimeUnit.SECONDS)) throw new IOException("Test peer timed out");
        } catch (InterruptedException e) { Thread.currentThread().interrupt(); throw new IOException(e); }
    }

    private static final class Peer implements TouchStream.Connection {
        final CountDownLatch connectEntered = new CountDownLatch(1);
        final CountDownLatch allowConnect;
        final CountDownLatch writeEntered = new CountDownLatch(1);
        final CountDownLatch allowWrite = new CountDownLatch(1);
        final CountDownLatch closed = new CountDownLatch(1);
        final AtomicBoolean closing = new AtomicBoolean();
        final AtomicBoolean failNext = new AtomicBoolean();
        final AtomicInteger closeCount = new AtomicInteger();
        final List<String> sent = new ArrayList<>();
        volatile String blockedPhase;
        boolean failConnect;
        Peer(boolean holdConnect) { allowConnect = new CountDownLatch(holdConnect ? 1 : 0); }
        @Override public void connect() throws IOException {
            connectEntered.countDown();
            await(allowConnect);
            if (closing.get() || failConnect) throw new IOException("Connect closed/failed");
        }
        @Override public void send(String command) throws IOException {
            if (blockedPhase != null && command.startsWith("touch " + blockedPhase + " ")) {
                writeEntered.countDown();
                await(allowWrite);
            }
            if (closing.get() || failNext.getAndSet(false)) throw new IOException("Write closed/failed");
            synchronized (sent) { sent.add(command); }
        }
        @Override public void close() {
            closeCount.incrementAndGet();
            closing.set(true);
            allowConnect.countDown(); allowWrite.countDown(); closed.countDown();
        }
        List<String> commands() { synchronized (sent) { return new ArrayList<>(sent); } }
    }

    private static final class Rig implements AutoCloseable {
        final ExecutorService worker = Executors.newSingleThreadExecutor();
        final ScheduledExecutorService timer = Executors.newSingleThreadScheduledExecutor();
        final AtomicInteger created = new AtomicInteger(), errors = new AtomicInteger();
        final CountDownLatch error = new CountDownLatch(1);
        final TouchStream stream;
        Rig(Peer... peers) {
            stream = new TouchStream(() -> {
                int index = created.getAndIncrement();
                if (index >= peers.length) throw new IOException("Unexpected reconnect");
                return peers[index];
            }, message -> { errors.incrementAndGet(); error.countDown(); }, worker, timer, 1000);
        }
        void drained() throws Exception { worker.submit(() -> { }).get(5, TimeUnit.SECONDS); }
        @Override public void close() {
            stream.close();
            try {
                check(worker.awaitTermination(5, TimeUnit.SECONDS), "input worker stopped");
                check(timer.awaitTermination(5, TimeUnit.SECONDS), "close timer stopped");
            } catch (InterruptedException e) { Thread.currentThread().interrupt(); throw new AssertionError(e); }
        }
    }

    private static void balanced(List<String> commands) {
        boolean held = false;
        for (String command : commands) {
            check(command.endsWith("\n"), "complete line protocol");
            String phase = command.split(" ")[1];
            switch (phase) {
                case "down": check(!held, "previous touch released before next press"); held = true; break;
                case "move": check(held, "move has a preceding press"); break;
                case "up": held = false; break;
                default: throw new AssertionError("Unexpected phase " + phase);
            }
        }
        check(!held, "final touch released");
    }
    private static void send(TouchStream stream, TouchStream.Phase phase, float x) {
        stream.send(phase, x, .75f, "bottom");
    }

    private static void moveBurst() throws Exception {
        Peer peer = new Peer(true);
        try (Rig rig = new Rig(peer)) {
            rig.stream.start();
            send(rig.stream, TouchStream.Phase.DOWN, .1f);
            await(peer.connectEntered);
            for (int i = 0; i < 10000; ++i) send(rig.stream, TouchStream.Phase.MOVE, i / 10000f);
            send(rig.stream, TouchStream.Phase.UP, .9999f);
            peer.allowConnect.countDown(); rig.drained();
            List<String> commands = peer.commands();
            check(commands.size() == 4, "move burst coalesced without losing press/release");
            check(commands.get(1).equals("touch down 0.100000 0.750000 bottom\n"), "press position and edge retained");
            check(commands.get(2).equals("touch move 0.999900 0.750000 bottom\n"), "most recent move retained");
            balanced(commands);
            check(rig.errors.get() == 0, "slow peer did not fail");
        }
    }

    private static void gestureOverflow() throws Exception {
        Peer peer = new Peer(true);
        try (Rig rig = new Rig(peer)) {
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .1f);
            await(peer.connectEntered);
            for (int i = 0; i < 1000; ++i) {
                send(rig.stream, TouchStream.Phase.UP, .2f);
                send(rig.stream, TouchStream.Phase.DOWN, .3f);
            }
            send(rig.stream, TouchStream.Phase.UP, .4f);
            peer.allowConnect.countDown(); rig.drained();
            List<String> commands = peer.commands();
            check(commands.size() <= TouchStream.MAX_PENDING + 4, "overflow has bounded backlog");
            check(commands.get(2).equals("touch up 0.100000 0.750000 bottom\n"), "overflow reset releases at the last delivered position");
            balanced(commands);
            check(rig.errors.get() == 0, "overflow did not break connection");
        }
    }

    private static void releaseOnStop() throws Exception {
        Peer peer = new Peer(false);
        try (Rig rig = new Rig(peer)) {
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .25f); rig.drained();
            rig.stream.stop(); await(peer.closed);
            List<String> commands = peer.commands();
            check(commands.get(commands.size() - 1).equals("touch up 0.250000 0.750000 bottom\n"), "stop delivers release before close");
            balanced(commands);
            rig.stream.stop();
            check(peer.closeCount.get() == 1, "repeated stop is idempotent");
        }
    }

    private static void stalledReleaseAndRestart() throws Exception {
        Peer old = new Peer(false), next = new Peer(false);
        try (Rig rig = new Rig(old, next)) {
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .1f); rig.drained();
            old.blockedPhase = "up";
            rig.stream.stop(); await(old.writeEntered);
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .8f); send(rig.stream, TouchStream.Phase.UP, .9f);
            await(old.closed); rig.drained();
            check(old.closeCount.get() == 1, "deadline interrupts stalled release once");
            check(!next.closing.get(), "old close deadline cannot close new connection");
            check(next.commands().get(1).contains("down 0.800000"), "new session delivers new press");
            balanced(next.commands());
            check(rig.errors.get() == 0, "retiring session does not report into new session");
        }
    }

    private static void stopDuringConnect() throws Exception {
        Peer old = new Peer(true), next = new Peer(false);
        try (Rig rig = new Rig(old, next)) {
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .1f); await(old.connectEntered);
            rig.stream.stop(); rig.stream.start();
            send(rig.stream, TouchStream.Phase.DOWN, .7f); send(rig.stream, TouchStream.Phase.UP, .8f);
            await(old.closed); rig.drained();
            check(old.commands().isEmpty(), "retired connect cannot replay stale press");
            check(next.commands().get(1).contains("down 0.700000"), "restart proceeds after canceled connect");
            balanced(next.commands());
            check(rig.errors.get() == 0, "canceled connect produces no stale warning");
        }
    }

    private static void reconnectMidGesture() throws Exception {
        Peer first = new Peer(false), second = new Peer(false);
        try (Rig rig = new Rig(first, second)) {
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .1f); rig.drained();
            first.failNext.set(true); send(rig.stream, TouchStream.Phase.MOVE, .2f); await(rig.error); rig.drained();
            check(first.closeCount.get() == 1, "failed socket closed");
            send(rig.stream, TouchStream.Phase.MOVE, .5f); send(rig.stream, TouchStream.Phase.UP, .6f); rig.drained();
            List<String> commands = second.commands();
            check(commands.size() == 3 && commands.get(1).contains("down 0.500000"), "reconnect resets and re-establishes held touch");
            balanced(commands);
            check(rig.errors.get() == 1, "one failure report");
        }
    }

    private static void failedConnectDropsBacklog() throws Exception {
        Peer first = new Peer(true), second = new Peer(false);
        first.failConnect = true;
        try (Rig rig = new Rig(first, second)) {
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .1f); await(first.connectEntered);
            send(rig.stream, TouchStream.Phase.MOVE, .2f); send(rig.stream, TouchStream.Phase.UP, .3f);
            first.allowConnect.countDown(); await(rig.error); rig.drained();
            send(rig.stream, TouchStream.Phase.DOWN, .8f); send(rig.stream, TouchStream.Phase.UP, .9f); rig.drained();
            check(second.commands().size() == 3 && second.commands().get(1).contains("down 0.800000"), "failed-connect backlog is not replayed");
            balanced(second.commands());
        }
    }

    private static void idleAndDestroy() throws Exception {
        try (Rig rig = new Rig()) {
            rig.stream.start(); rig.stream.stop(); rig.stream.start(); rig.stream.stop(); rig.drained();
            check(rig.created.get() == 0, "idle lifecycle never connects");
            send(rig.stream, TouchStream.Phase.DOWN, .1f); rig.drained();
            check(rig.created.get() == 0, "inactive input ignored");
        }
        Peer peer = new Peer(false);
        try (Rig rig = new Rig(peer)) {
            rig.stream.start(); send(rig.stream, TouchStream.Phase.DOWN, .4f); rig.drained();
            rig.stream.close(); await(peer.closed);
            balanced(peer.commands());
            check(peer.closeCount.get() == 1, "destroy permits final release and closes socket");
        }
    }

    public static void main(String[] args) throws Exception {
        Locale old = Locale.getDefault();
        try {
            Locale.setDefault(Locale.GERMANY);
            moveBurst(); gestureOverflow(); releaseOnStop(); stalledReleaseAndRestart();
            stopDuringConnect(); reconnectMidGesture(); failedConnectDropsBacklog(); idleAndDestroy();
        } finally { Locale.setDefault(old); }
        System.out.println("Native touch transport: " + checks + " assertions passed");
    }
}
