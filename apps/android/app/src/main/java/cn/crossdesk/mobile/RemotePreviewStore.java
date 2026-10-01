package cn.crossdesk.mobile;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Handler;
import android.os.Looper;
import android.util.AtomicFile;
import org.json.JSONArray;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Consumer;

/** Process-wide, serialized preview I/O. Clears invalidate even captures not yet queued for saving. */
final class RemotePreviewStore {
    static final String OPTION="saveRemotePreviews";
    static final int WIDTH=640,HEIGHT=360;
    private static RemotePreviewStore instance;
    static synchronized RemotePreviewStore get(Context context){
        if(instance==null){
            Context app=context.getApplicationContext();
            instance=new RemotePreviewStore(new File(app.getNoBackupFilesDir(),"RecentConnectionThumbnails"),
                app.getSharedPreferences("settings",Context.MODE_PRIVATE),app.getSharedPreferences("remote-previews",Context.MODE_PRIVATE));
            instance.enforceConsent();
        }
        return instance;
    }
    static final class Capture {
        final String key;final long generation,version;
        private Capture(String key,long generation,long version){this.key=key;this.generation=generation;this.version=version;}
    }
    final File directory;
    final ExecutorService io=Executors.newSingleThreadExecutor(r->new Thread(r,"CrossDesk previews"));
    private final Handler main=new Handler(Looper.getMainLooper());
    private final SharedPreferences options,index;
    private final Set<String> files=new HashSet<>();
    private final Map<String,Long> versions=new HashMap<>();
    private final Set<Runnable> listeners=new CopyOnWriteArraySet<>();
    private long generation;
    volatile boolean cleaning;
    volatile String cleanupError="";
    RemotePreviewStore(File directory,SharedPreferences options,SharedPreferences index){
        this.directory=directory;this.options=options;this.index=index;
        for(String key:index.getStringSet("files",Collections.emptySet()))if(key.matches("[a-f0-9]{64}"))files.add(key);
    }
    boolean enabled(){return options.getBoolean("networkConsent",false)&&options.getBoolean(OPTION,false);}
    void addListener(Runnable listener){listeners.add(listener);}
    void removeListener(Runnable listener){listeners.remove(listener);}
    private void changed(){main.post(()->{for(Runnable listener:listeners)listener.run();});}
    synchronized void enforceConsent(){
        if(!options.getBoolean("networkConsent",false))options.edit().putBoolean(OPTION,false).apply();
        if(!enabled()&&!cleaning)clear();
    }
    void setEnabled(boolean enabled){
        options.edit().putBoolean(OPTION,enabled&&options.getBoolean("networkConsent",false)).apply();
        if(!enabled())clear();else changed();
    }
    synchronized Capture begin(String server,String id){
        if(!enabled()||cleaning||id.isEmpty())return null;
        String key=key(server,id);long version=versions.getOrDefault(key,0L)+1;versions.put(key,version);
        return new Capture(key,generation,version);
    }
    synchronized boolean current(Capture capture){
        return capture!=null&&enabled()&&!cleaning&&capture.generation==generation&&
            capture.version==versions.getOrDefault(capture.key,0L);
    }
    /** Takes ownership of the small bitmap; JPEG compression and writes never run on the UI thread. */
    void save(Capture capture,Bitmap bitmap){
        io.execute(()->{
            boolean saved=false;
            try{
                if(current(capture)&&bitmap.getWidth()==WIDTH&&bitmap.getHeight()==HEIGHT)saved=write(capture.key,bitmap);
            }finally{bitmap.recycle();}
            synchronized(this){
                if(!saved||!current(capture))return;
                Set<String> next=new HashSet<>(files);next.add(capture.key);
                if(!index.edit().putStringSet("files",next).commit())return;
                files.add(capture.key);
            }
            changed();
        });
    }
    private boolean write(String key,Bitmap bitmap){
        AtomicFile file=new AtomicFile(new File(directory,key+".jpg"));FileOutputStream output=null;
        try{
            ByteArrayOutputStream encoded=new ByteArrayOutputStream();
            if(!bitmap.compress(Bitmap.CompressFormat.JPEG,78,encoded))return false;
            byte[] bytes=encoded.toByteArray();
            if(!directory.isDirectory()&&!directory.mkdirs())return false;
            output=file.startWrite();output.write(bytes);
            output.getFD().sync();file.finishWrite(output);output=null;
            return Arrays.equals(bytes,file.readFully());
        }catch(IOException|RuntimeException error){if(output!=null)file.failWrite(output);return false;}
    }
    void load(String server,String id,Consumer<Bitmap> receive){
        final Capture read;
        synchronized(this){
            String key=key(server,id);
            if(!enabled()||cleaning||!files.contains(key)){main.post(()->receive.accept(null));return;}
            read=new Capture(key,generation,versions.getOrDefault(key,0L));
        }
        io.execute(()->{
            Bitmap bitmap=null;
            if(current(read)){
                File file=new File(directory,read.key+".jpg");
                if(file.isFile()&&file.length()<=2*1024*1024){
                    BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;
                    BitmapFactory.decodeFile(file.getAbsolutePath(),bounds);
                    if(bounds.outWidth==WIDTH&&bounds.outHeight==HEIGHT)bitmap=BitmapFactory.decodeFile(file.getAbsolutePath());
                }
            }
            Bitmap image=bitmap;
            main.post(()->{
                boolean valid;synchronized(this){valid=current(read)&&files.contains(read.key);}
                if(valid)receive.accept(image);else if(image!=null)image.recycle();
            });
        });
    }
    synchronized void remove(String server,String id){
        String key=key(server,id);versions.put(key,versions.getOrDefault(key,0L)+1);files.remove(key);
        io.execute(()->{
            new AtomicFile(new File(directory,key+".jpg")).delete();
            synchronized(this){
                boolean saved=index.edit().putStringSet("files",new HashSet<>(files)).commit();
                if(!saved||new File(directory,key+".jpg").exists())cleanupError="部分预览图未能清除，请重试。";
            }
            changed();
        });changed();
    }
    synchronized void clear(){
        final long epoch=++generation;files.clear();cleaning=true;cleanupError="";changed();
        io.execute(()->{
            boolean success=index.edit().clear().commit();
            try{deleteTree(directory);}catch(IOException error){success=false;}
            synchronized(this){
                if(epoch!=generation)return;
                cleaning=false;cleanupError=success?"":"部分预览图未能清除，请重试。";
            }
            changed();
        });
    }
    private static void deleteTree(File file)throws IOException{
        if(!file.exists())return;
        if(file.isDirectory()&&!Files.isSymbolicLink(file.toPath())){
            File[] children=file.listFiles();if(children==null)throw new IOException("Cannot list previews");
            boolean failed=false;for(File child:children)try{deleteTree(child);}catch(IOException error){failed=true;}
            if(failed)throw new IOException("Cannot clear all previews");
        }
        if(!file.delete())throw new IOException("Cannot delete preview");
    }
    private static String key(String server,String id){
        try{
            byte[] bytes=new JSONArray().put(server).put(id).toString().getBytes(StandardCharsets.UTF_8);
            byte[] hash=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder key=new StringBuilder();
            for(byte b:hash)key.append(String.format(Locale.ROOT,"%02x",b&255));return key.toString();
        }catch(Exception error){throw new IllegalStateException(error);}
    }
}
