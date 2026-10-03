package cn.crossdesk.mobile;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.core.app.ActivityScenario;
import androidx.lifecycle.Lifecycle;
import androidx.test.platform.app.InstrumentationRegistry;
import android.app.Dialog;
import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class AppUpdateCheckerTest {
    private static final class Fixture implements RemoteVersionCheck.Fetcher {
        int cancelled;
        final List<Consumer<byte[]>> replies = new ArrayList<>();
        final AppUpdateChecker checker = new AppUpdateChecker("1.6.0", this,
                (version, data) -> new String(data, StandardCharsets.UTF_8), () -> {});
        public RemoteVersionCheck.Cancellation fetch(Consumer<byte[]> reply) {
            replies.add(reply); return () -> cancelled++;
        }
        void reply(String result) { replies.get(replies.size()-1).accept(result == null ? null : result.getBytes(StandardCharsets.UTF_8)); }
    }

    @Test public void everyVisitChecksOnceAndConcurrentTriggersDeduplicate() {
        Fixture f = new Fixture();
        f.checker.checkNow(); assertTrue(f.replies.isEmpty());
        f.checker.setEnabled(true); f.checker.checkNow(); f.checker.setEnabled(true);
        assertEquals(1, f.replies.size());
        f.reply("1.7.0"); assertTrue(f.checker.updateAvailable());
        f.checker.setEnabled(true); assertEquals(1, f.replies.size());
        f.checker.setEnabled(false); f.checker.setEnabled(true);
        assertEquals(2, f.replies.size());
        f.reply(""); assertFalse(f.checker.updateAvailable());
        assertEquals(AppUpdateChecker.Status.UP_TO_DATE, f.checker.status);
        f.checker.checkNow(); assertEquals(3, f.replies.size());
    }

    @Test public void failedChecksRetainBadgeUntilAnotherExplicitCheck() {
        Fixture f = new Fixture(); f.checker.setEnabled(true); f.reply("1.7.0");
        f.checker.checkNow(); f.reply(null);
        assertEquals(AppUpdateChecker.Status.FAILED, f.checker.status);
        assertTrue(f.checker.updateAvailable());
        f.checker.setEnabled(true); assertEquals(2, f.replies.size());
        f.checker.checkNow(); assertEquals(3, f.replies.size());
        f.reply(""); assertFalse(f.checker.updateAvailable());
    }

    @Test public void backgroundAndRevocationCancelAndIgnoreStaleReplies() {
        Fixture f = new Fixture(); f.checker.setEnabled(true); f.checker.setEnabled(false);
        assertEquals(1, f.cancelled);
        f.reply("9.0.0"); assertFalse(f.checker.updateAvailable());
        f.checker.setEnabled(true); assertEquals(2, f.replies.size());
        f.replies.get(0).accept("9.0.0".getBytes(StandardCharsets.UTF_8));
        assertEquals(AppUpdateChecker.Status.CHECKING, f.checker.status);
        f.reply("1.7.0"); f.checker.setEnabled(false); f.checker.checkNow();
        assertEquals(2, f.replies.size()); f.checker.setEnabled(true);
        assertEquals(3, f.replies.size()); assertTrue(f.checker.updateAvailable());
    }

    @Test public void nativeParserSeparatesMobileFromDesktopAndInvalidMetadata() {
        byte[] manifest = "{\"latest_version\":\"99.0.0\",\"patch\":99,\"downloads\":{\"ios-arm64\":{\"version\":\"0.0.2\"},\"android-arm64\":{\"version\":\"1.6.0-2-20261002\"}}}".getBytes(StandardCharsets.UTF_8);
        assertEquals("1.6.0-2", NativeSession.nCheckMobileUpdate("1.6.0", manifest));
        assertEquals("", NativeSession.nCheckMobileUpdate("1.6.0-2", manifest));
        assertEquals("", NativeSession.nCheckMobileUpdate("1.7.0", manifest));
        assertNull(NativeSession.nCheckMobileUpdate("", manifest));
        assertNull(NativeSession.nCheckMobileUpdate("1.6.0", "{\"version\":\"99.0.0\"}".getBytes(StandardCharsets.UTF_8)));
    }

    @Test public void settingsAndAboutReflectUpdatesWithoutReopeningThePage() {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        var preferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE);
        boolean hadConsent = preferences.contains("networkConsent");
        boolean consent = preferences.getBoolean("networkConsent", false);
        preferences.edit().putBoolean("networkConsent", false).commit();
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                ((Dialog)field(activity, "privacyDialog")).dismiss();
                List<Consumer<byte[]>> replies = new ArrayList<>();
                AppUpdateChecker checker = new AppUpdateChecker("1.6.0",
                        reply -> { replies.add(reply); return () -> {}; },
                        (version, data) -> new String(data, StandardCharsets.UTF_8),
                        () -> invoke(activity, "renderAppUpdates"));
                try {
                    var field = MainActivity.class.getDeclaredField("appUpdates");
                    field.setAccessible(true); field.set(activity, checker);
                } catch (Exception error) { throw new AssertionError(error); }
                assertEquals(View.GONE, ((View)field(activity, "settingsUpdateDot")).getVisibility());
                checker.setEnabled(true);
                replies.get(0).accept("1.7.0".getBytes(StandardCharsets.UTF_8));
                assertEquals(View.VISIBLE, ((View)field(activity, "settingsUpdateDot")).getVisibility());
                assertEquals("设置，有新版本", ((View)field(activity, "settingsUpdateButton")).getContentDescription());
                ((View)field(activity, "settingsUpdateButton")).performClick();
                assertEquals(View.VISIBLE, ((View)field(activity, "aboutUpdateDot")).getVisibility());
                ((View)field(activity, "aboutUpdateRow")).performClick();
                assertNotNull(findText(activity.getWindow().getDecorView(), "新版本可用：v1.7.0"));
                checker.checkNow(); replies.get(1).accept(null);
                assertNotNull(findText(activity.getWindow().getDecorView(), "新版本可用：v1.7.0"));
                checker.checkNow(); replies.get(2).accept(new byte[0]);
                invoke(activity, "settings");
                assertEquals(View.GONE, ((View)field(activity, "aboutUpdateDot")).getVisibility());
                checker.setEnabled(false);
            });
        } finally {
            if (hadConsent) preferences.edit().putBoolean("networkConsent", consent).commit();
            else preferences.edit().remove("networkConsent").commit();
        }
    }

    @Test public void activityVisitsAndConnectionTeardownTriggerChecks() {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        var preferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE);
        boolean hadConsent = preferences.contains("networkConsent");
        boolean consent = preferences.getBoolean("networkConsent", false);
        preferences.edit().putBoolean("networkConsent", false).commit();
        Fixture f = new Fixture();
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                ((Dialog)field(activity, "privacyDialog")).dismiss();
                try {
                    var field = MainActivity.class.getDeclaredField("appUpdates");
                    field.setAccessible(true); field.set(activity, f.checker);
                } catch (Exception error) { throw new AssertionError(error); }
            });
            scenario.moveToState(Lifecycle.State.CREATED);
            preferences.edit().putBoolean("networkConsent", true).commit();
            scenario.moveToState(Lifecycle.State.RESUMED);
            scenario.onActivity(activity -> {
                assertEquals(1, f.replies.size()); f.reply("1.7.0");
                endConnection(activity);
                assertEquals(2, f.replies.size()); f.reply(null);
                assertTrue(f.checker.updateAvailable());
                endConnection(activity);
                assertEquals(3, f.replies.size()); f.reply("");
                assertFalse(f.checker.updateAvailable());
            });
            scenario.moveToState(Lifecycle.State.CREATED);
            scenario.onActivity(activity -> {
                endConnection(activity); assertEquals(3, f.replies.size());
            });
            scenario.moveToState(Lifecycle.State.RESUMED);
            scenario.onActivity(activity -> assertEquals(4, f.replies.size()));
        } finally {
            if (hadConsent) preferences.edit().putBoolean("networkConsent", consent).commit();
            else preferences.edit().remove("networkConsent").commit();
        }
    }

    private static void endConnection(MainActivity activity) {
        try {
            var method = MainActivity.class.getDeclaredMethod("end", String.class);
            method.setAccessible(true); method.invoke(activity, "");
        } catch (Exception error) { throw new AssertionError(error); }
    }

    private static Object field(MainActivity activity, String name) {
        try { var field = MainActivity.class.getDeclaredField(name); field.setAccessible(true); return field.get(activity); }
        catch (Exception error) { throw new AssertionError(error); }
    }
    private static void invoke(MainActivity activity, String name) {
        try { var method = MainActivity.class.getDeclaredMethod(name); method.setAccessible(true); method.invoke(activity); }
        catch (Exception error) { throw new AssertionError(error); }
    }
    private static TextView findText(View view, String text) {
        if (view instanceof TextView && ((TextView)view).getText().toString().equals(text)) return (TextView)view;
        if (view instanceof ViewGroup) for (int i = 0; i < ((ViewGroup)view).getChildCount(); i++) {
            TextView found = findText(((ViewGroup)view).getChildAt(i), text);
            if (found != null) return found;
        }
        return null;
    }
}
