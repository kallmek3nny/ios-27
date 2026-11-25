package org.ios27.host;

import java.io.IOException;
import java.util.ArrayDeque;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/** Serial, bounded SimulatorHID input. Lifecycle methods run on the owner/UI
 * thread; socket work runs on the worker. Closing a connection must interrupt
 * its pending connect/write, so stop never waits on the UI thread. */
final class TouchStream implements AutoCloseable {
    static final int MAX_PENDING = 64;
    enum Phase { DOWN, MOVE, UP }
    interface Connection extends AutoCloseable {
        void connect() throws IOException;
        void send(String command) throws IOException;
        @Override void close() throws IOException;
    }
    interface Factory { Connection create() throws IOException; }

    private static final class Event {
        final Phase phase;
        final float x, y;
        final String edge;
        Event(Phase phase, float x, float y, String edge) {
            if (phase == null || !Float.isFinite(x) || !Float.isFinite(y) || x < 0 || x > 1 || y < 0 || y > 1 ||
                    !("none".equals(edge) || "top".equals(edge) || "bottom".equals(edge) ||
                      "left".equals(edge) || "right".equals(edge))) throw new IllegalArgumentException("Invalid touch");
            this.phase = phase; this.x = x; this.y = y; this.edge = edge;
        }
        Event as(Phase next) { return new Event(next, x, y, edge); }
        String command() {
            return String.format(Locale.ROOT, "touch %s %.6f %.6f %s\n",
                    phase.name().toLowerCase(Locale.ROOT), x, y, edge);
        }
    }
    private static final Event RESET = new Event(Phase.UP, 0, 0, "none");
    private static final class Session {
        final ArrayDeque<Event> pending = new ArrayDeque<>();
        Event latest = RESET;
        Event wirePosition = RESET;
        Connection connection;
        boolean scheduled, closing, closed, pressed, wireDown;
    }

    private final Factory factory;
    private final Consumer<String> report;
    private final ExecutorService worker;
    private final ScheduledExecutorService closer;
    private final long closeMillis;
    private Session active;
    private boolean destroyed;

    TouchStream(Factory factory, Consumer<String> report) {
        this(factory, report, Executors.newSingleThreadExecutor(), Executors.newSingleThreadScheduledExecutor(), 250);
    }
    TouchStream(Factory factory, Consumer<String> report, ExecutorService worker,
                ScheduledExecutorService closer, long closeMillis) {
        if (closeMillis <= 0) throw new IllegalArgumentException("Invalid close deadline");
        this.factory = factory; this.report = report; this.worker = worker;
        this.closer = closer; this.closeMillis = closeMillis;
    }

    void start() {
        stop();
        synchronized (this) {
            if (destroyed) throw new IllegalStateException("Touch stream is closed");
            active = new Session();
        }
    }

    void send(Phase phase, float x, float y, String edge) {
        Event event = new Event(phase, x, y, edge);
        Session session;
        synchronized (this) {
            session = active;
            if (destroyed || session == null) return;
            if (phase != Phase.DOWN && !session.pressed) return;
            session.pressed = phase != Phase.UP;
            session.latest = event;
            if (phase == Phase.MOVE && !session.pending.isEmpty() && session.pending.peekLast().phase == Phase.MOVE)
                session.pending.removeLast();
            if (session.pending.size() == MAX_PENDING) {
                // Discard obsolete gestures as a unit and reset the wire state.
                // Dropping an arbitrary DOWN/UP can leave a finger held forever.
                session.pending.clear();
                session.pending.add(RESET);
                if (session.pressed) session.pending.add(event.as(Phase.DOWN));
            } else session.pending.add(event);
        }
        schedule(session);
    }

    private void schedule(Session session) {
        synchronized (this) {
            if (session.closed || session.scheduled) return;
            session.scheduled = true;
        }
        try { worker.execute(() -> drain(session)); }
        catch (RejectedExecutionException ignored) { finish(session); }
    }

    private void drain(Session session) {
        for (;;) {
            Event event;
            Connection connection;
            boolean ending;
            synchronized (this) {
                if (session.closed) return;
                ending = session.closing;
                connection = session.connection;
                event = ending ? session.latest.as(Phase.UP) : session.pending.pollFirst();
                if (event == null) { session.scheduled = false; return; }
            }
            if (ending) {
                try { if (connection != null) connection.send(event.command()); }
                catch (IOException ignored) { }
                finally { finish(session); }
                return;
            }
            try {
                if (connection == null) {
                    connection = factory.create();
                    boolean discard;
                    synchronized (this) {
                        discard = session.closed || session.closing;
                        if (!discard) { session.connection = connection; session.wireDown = false; }
                    }
                    if (discard) { close(connection); continue; }
                    connection.connect();
                    connection.send(RESET.command());
                }
                synchronized (this) {
                    if (session.closed) return;
                    if (session.closing) continue;
                }
                if (event == RESET) event = session.wirePosition.as(Phase.UP);
                if (event.phase == Phase.MOVE && !session.wireDown) event = event.as(Phase.DOWN);
                if (event.phase == Phase.DOWN && session.wireDown) connection.send(session.wirePosition.as(Phase.UP).command());
                connection.send(event.command());
                session.wireDown = event.phase != Phase.UP;
                session.wirePosition = event;
            } catch (IOException | RuntimeException error) {
                disconnect(session, connection);
                boolean closing, notify;
                synchronized (this) {
                    session.pending.clear();
                    session.scheduled = false;
                    closing = session.closing;
                    notify = active == session && !destroyed && !session.closed && !closing;
                }
                if (closing) finish(session);
                if (notify) report.accept("Native frame preview · touch connection unavailable");
                return;
            }
        }
    }

    private static void close(Connection connection) {
        if (connection != null) try { connection.close(); } catch (IOException ignored) { }
    }
    private void disconnect(Session session, Connection connection) {
        boolean owned;
        synchronized (this) {
            owned = session.connection == connection;
            if (owned) session.connection = null;
        }
        if (owned) close(connection);
    }
    private void finish(Session session) {
        Connection connection;
        synchronized (this) {
            session.closed = true; session.scheduled = false; session.pending.clear();
            connection = session.connection; session.connection = null;
        }
        close(connection);
    }

    void stop() {
        Session session;
        synchronized (this) {
            session = active; active = null;
            if (session == null) return;
            session.closing = true; session.pending.clear(); session.pressed = false;
            if (session.connection == null && !session.scheduled) { session.closed = true; return; }
        }
        schedule(session); // sends the release even though the preview is inactive
        try { closer.schedule(() -> finish(session), closeMillis, TimeUnit.MILLISECONDS); }
        catch (RejectedExecutionException ignored) { finish(session); }
    }

    @Override public void close() {
        synchronized (this) {
            if (destroyed) return;
            destroyed = true;
        }
        stop();
        // Keep queued release work and its close deadline alive during teardown.
        worker.shutdown(); closer.shutdown();
    }
}
