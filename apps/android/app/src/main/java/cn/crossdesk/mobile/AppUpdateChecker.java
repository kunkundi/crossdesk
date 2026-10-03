package cn.crossdesk.mobile;

import java.util.function.BiFunction;

/** Main-thread local-app checks triggered by visits, session endings and manual requests. */
final class AppUpdateChecker {
    enum Status { IDLE, CHECKING, UP_TO_DATE, AVAILABLE, FAILED }
    private final String currentVersion;
    private final RemoteVersionCheck.Fetcher fetcher;
    private final BiFunction<String, byte[], String> compare;
    private final Runnable changed;
    private RemoteVersionCheck.Cancellation request;
    private boolean enabled;
    private long generation;
    Status status = Status.IDLE;
    String availableVersion = "";

    AppUpdateChecker(String currentVersion, Runnable changed) {
        this(currentVersion, RemoteVersionCheck.ReleaseRequest::start,
                NativeSession::nCheckMobileUpdate, changed);
    }

    AppUpdateChecker(String currentVersion, RemoteVersionCheck.Fetcher fetcher,
                     BiFunction<String, byte[], String> compare, Runnable changed) {
        this.currentVersion = currentVersion; this.fetcher = fetcher;
        this.compare = compare;
        this.changed = changed;
    }

    boolean updateAvailable() { return !availableVersion.isEmpty(); }

    void setEnabled(boolean value) {
        if (enabled == value) return;
        enabled = value;
        if (enabled) { checkNow(); return; }
        ++generation;
        if (request != null) request.cancel();
        request = null;
        if (status == Status.CHECKING) {
            status = updateAvailable() ? Status.AVAILABLE : Status.IDLE;
        }
        changed.run();
    }

    void checkNow() {
        if (!enabled || status == Status.CHECKING) return;
        status = Status.CHECKING;
        changed.run();
        long token = ++generation;
        request = fetcher.fetch(data -> {
            if (!enabled || token != generation) return;
            request = null;
            String result = data == null ? null : compare.apply(currentVersion, data);
            if (result == null) {
                status = Status.FAILED;
            } else {
                availableVersion = result;
                status = updateAvailable() ? Status.AVAILABLE : Status.UP_TO_DATE;
            }
            changed.run();
        });
    }
}
