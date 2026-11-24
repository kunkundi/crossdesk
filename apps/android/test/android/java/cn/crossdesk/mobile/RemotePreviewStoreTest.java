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
        store.clear();flush();store.io.shutdown();
        try(var paths=Files.walk(root.toPath())){for(var path:paths.sorted(Comparator.reverseOrder()).toList())Files.delete(path);}
        context.deleteSharedPreferences(prefix);context.deleteSharedPreferences(prefix+"-index");
    }
    private void flush()throws Exception{store.io.submit(()->{}).get(5,TimeUnit.SECONDS);InstrumentationRegistry.getInstrumentation().waitForIdleSync();}
    private void enable(){options.edit().putBoolean("networkConsent",true).commit();store.setEnabled(true);}
    private static Bitmap solid(int color){Bitmap image=Bitmap.createBitmap(640,360,Bitmap.Config.ARGB_8888);image.eraseColor(color);return image;}
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

    @Test public void userSeesWallpaperWithScreenPreviewsOff()throws Exception{
        // Given network consent and the default screen preview setting
        options.edit().putBoolean("networkConsent",true).commit();
        assertFalse(store.enabled());
        ByteArrayOutputStream jpeg=new ByteArrayOutputStream();
        Bitmap source=solid(Color.RED);
        try{assertTrue(source.compress(Bitmap.CompressFormat.JPEG,75,jpeg));}
        finally{source.recycle();}

        // When the computer supplies its wallpaper
        store.saveWallpaper("server:1","123456789",jpeg.toByteArray());
        flush();

        // Then its cover is visible only in the correct history and disappears on revocation
        Bitmap cover=load("server:1","123456789");
        assertNotNull(cover);
        try{assertTrue(Color.red(cover.getPixel(320,180))>240);}
        finally{cover.recycle();}
        assertNull(load("server:2","123456789"));
        options.edit().putBoolean("networkConsent",false).commit();
        store.enforceConsent();flush();
        options.edit().putBoolean("networkConsent",true).commit();
        assertNull(load("server:1","123456789"));
    }

    @Test public void disablingDeletesFilesAndInvalidatesInFlightCapture()throws Exception{
        enable();var capture=store.begin("server","1");store.save(capture,solid(Color.BLUE));flush();
        store.setEnabled(false);store.save(capture,solid(Color.RED));flush();
        assertFalse(store.enabled());assertFalse(store.directory.exists());assertTrue(index.getAll().isEmpty());assertNull(load("server","1"));
        store.setEnabled(true);assertNull(load("server","1"));
    }

}
