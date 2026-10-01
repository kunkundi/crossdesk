package cn.crossdesk.mobile;

import android.content.*;
import android.graphics.*;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.*;
import java.io.*;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.*;
import static org.junit.Assert.*;

public class RemotePreviewStoreTest {
    private Context context;
    private File root;
    private SharedPreferences options,index;
    private String prefix;
    private RemotePreviewStore store;
    @Before public void setup()throws Exception{
        context=InstrumentationRegistry.getInstrumentation().getTargetContext();prefix="preview-test-"+UUID.randomUUID();
        options=context.getSharedPreferences(prefix,0);index=context.getSharedPreferences(prefix+"-index",0);
        root=Files.createTempDirectory(context.getCacheDir().toPath(),"previews-").toFile();
        store=new RemotePreviewStore(new File(root,"images"),options,index);
    }
    @After public void cleanup()throws Exception{
        store.directory.setWritable(true,false);store.clear();flush();store.io.shutdown();
        try(var paths=Files.walk(root.toPath())){for(var path:paths.sorted(Comparator.reverseOrder()).toList())Files.delete(path);}
        context.deleteSharedPreferences(prefix);context.deleteSharedPreferences(prefix+"-index");
    }
    private void flush()throws Exception{store.io.submit(()->{}).get(5,TimeUnit.SECONDS);InstrumentationRegistry.getInstrumentation().waitForIdleSync();}
    private void enable(){options.edit().putBoolean("networkConsent",true).commit();store.setEnabled(true);}
    static Bitmap solid(int color){Bitmap image=Bitmap.createBitmap(640,360,Bitmap.Config.ARGB_8888);image.eraseColor(color);return image;}
    private Bitmap load(String server,String id)throws Exception{
        var result=new java.util.concurrent.atomic.AtomicReference<Bitmap>();var done=new CountDownLatch(1);
        store.load(server,id,image->{result.set(image);done.countDown();});assertTrue(done.await(5,TimeUnit.SECONDS));return result.get();
    }
    @Test public void offByDefaultAndConsentCannotBeBypassed()throws Exception{
        assertFalse(store.enabled());assertNull(store.begin("server","1"));store.setEnabled(true);flush();assertFalse(store.enabled());
        options.edit().putBoolean("networkConsent",true).commit();assertFalse(store.enabled());
        enable();assertNotNull(store.begin("server","1"));
        options.edit().putBoolean("networkConsent",false).commit();store.enforceConsent();flush();
        assertFalse(options.getBoolean(RemotePreviewStore.OPTION,true));assertNull(store.begin("server","1"));
    }
    @Test public void jpegPersistsAcrossStoreRecreationAndRemainsServerScoped()throws Exception{
        enable();var capture=store.begin("server:1","123456789");store.save(capture,solid(Color.GREEN));flush();
        assertTrue(new File(store.directory,capture.key+".jpg").isFile());
        assertNull(load("server:2","123456789"));assertNull(load("server:1","other"));
        store.io.shutdown();store=new RemotePreviewStore(store.directory,options,index);
        Bitmap loaded=load("server:1","123456789");assertNotNull(loaded);assertEquals(640,loaded.getWidth());assertEquals(360,loaded.getHeight());assertTrue(Color.green(loaded.getPixel(320,180))>240);loaded.recycle();
    }
    @Test public void clearRejectsQueuedAndLateCapturesWithoutTurningOffTheOption()throws Exception{
        enable();var old=store.begin("server","1");var gate=new CountDownLatch(1);var started=new CountDownLatch(1);
        store.io.execute(()->{started.countDown();try{gate.await(5,TimeUnit.SECONDS);}catch(InterruptedException e){Thread.currentThread().interrupt();}});
        assertTrue(started.await(5,TimeUnit.SECONDS));Bitmap queued=solid(Color.RED);store.save(old,queued);store.clear();
        assertFalse(store.current(old));assertNull(store.begin("server","2"));gate.countDown();flush();
        Bitmap late=solid(Color.RED);store.save(old,late);flush();assertTrue(queued.isRecycled());assertTrue(late.isRecycled());
        assertFalse(store.directory.exists());assertNull(load("server","1"));assertTrue(store.enabled());
        var next=store.begin("server","1");store.save(next,solid(Color.BLUE));flush();assertNotNull(load("server","1"));
    }
    @Test public void deletingARecordRejectsPendingSaveAndDoesNotRemoveOtherPreviews()throws Exception{
        enable();var first=store.begin("server","1");var second=store.begin("server","2");
        store.save(first,solid(Color.RED));store.save(second,solid(Color.BLUE));flush();
        store.remove("server","1");store.save(first,solid(Color.GREEN));flush();
        assertNull(load("server","1"));Bitmap other=load("server","2");assertNotNull(other);other.recycle();
        assertFalse(new File(store.directory,first.key+".jpg").exists());
    }
    @Test public void aNewConnectionSupersedesAnOlderCaptureForTheSameDevice()throws Exception{
        enable();var old=store.begin("server","1");var current=store.begin("server","1");
        store.save(current,solid(Color.GREEN));store.save(old,solid(Color.RED));flush();
        Bitmap loaded=load("server","1");assertNotNull(loaded);assertTrue(Color.green(loaded.getPixel(320,180))>240);loaded.recycle();
    }
    @Test public void disablingDeletesFilesAndInvalidatesInFlightCapture()throws Exception{
        enable();var capture=store.begin("server","1");store.save(capture,solid(Color.BLUE));flush();
        store.setEnabled(false);store.save(capture,solid(Color.RED));flush();
        assertFalse(store.enabled());assertFalse(store.directory.exists());assertTrue(index.getAll().isEmpty());assertNull(load("server","1"));
        store.setEnabled(true);assertNull(load("server","1"));
    }
    @Test public void failedCleanupHidesOldImagesAndCanBeRetried()throws Exception{
        enable();var capture=store.begin("server","1");store.save(capture,solid(Color.GREEN));flush();
        assertTrue(store.directory.setWritable(false,false));store.clear();flush();assertFalse(store.cleaning);assertFalse(store.cleanupError.isEmpty());
        assertNull(load("server","1"));assertTrue(store.directory.setWritable(true,false));store.clear();flush();
        assertEquals("",store.cleanupError);assertFalse(store.directory.exists());
    }
    @Test public void corruptImagesAndFailedWritesNeverBecomePreviewCards()throws Exception{
        enable();assertTrue(store.directory.createNewFile());var capture=store.begin("server","1");store.save(capture,solid(Color.RED));flush();assertNull(load("server","1"));
        assertTrue(store.directory.delete());store.save(capture,solid(Color.BLUE));flush();
        Files.write(new File(store.directory,capture.key+".jpg").toPath(),new byte[]{1,2,3});assertNull(load("server","1"));
    }
    @Test public void cropUsesCenteredSixteenByNineWithoutStretching(){
        assertEquals(new Rect(0,0,3840,2160),RemoteVideoView.previewCrop(3840,2160));
        assertEquals(new Rect(0,75,800,525),RemoteVideoView.previewCrop(800,600));
        assertEquals(new Rect(320,0,2240,1080),RemoteVideoView.previewCrop(2560,1080));
        assertEquals(new Rect(0,547,900,1053),RemoteVideoView.previewCrop(900,1600));
    }
}
