package cn.crossdesk.mobile;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import java.io.*;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Private session files; document-provider reads/writes never run on UI/RTC threads. */
final class SessionDocuments {
    interface Prepared { void ready(File file,String name); void failed(); }
    private final Context context;
    private final ExecutorService io=Executors.newSingleThreadExecutor(r->new Thread(r,"CrossDesk documents"));
    final File directory;
    private volatile boolean closed;
    SessionDocuments(Context context){
        this.context=context.getApplicationContext();
        directory=new File(context.getCacheDir(),"transfers/"+UUID.randomUUID());
    }
    void prepare(Uri uri,Prepared callback){
        io.execute(()->{
            File staged=null;
            try{
                if(closed)return;
                String name="file";
                try(Cursor cursor=context.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
                    if(cursor!=null&&cursor.moveToFirst())name=cursor.getString(0);
                }
                name=safeName(name);
                Files.createDirectories(directory.toPath());
                staged=File.createTempFile("sending-",".part",directory);
                try(InputStream input=context.getContentResolver().openInputStream(uri);OutputStream output=new FileOutputStream(staged)){
                    if(input==null)throw new IOException("No input");
                    copy(input,output);
                }
                if(closed){Files.deleteIfExists(staged.toPath());return;}
                callback.ready(staged,name);
            }catch(Exception error){
                if(staged!=null)staged.delete();
                if(!closed)callback.failed();
            }
        });
    }
    void save(File file,Uri uri,java.util.function.Consumer<Boolean> completion){
        io.execute(()->{
            boolean success=false;
            try{
                if(closed)return;
                try(InputStream input=new FileInputStream(file);OutputStream output=context.getContentResolver().openOutputStream(uri,"wt")){
                    if(output==null)throw new IOException("No output");
                    copy(input,output);
                }
                success=true;
            }catch(Exception ignored){ }
            if(!closed)completion.accept(success);
        });
    }
    private void copy(InputStream input,OutputStream output)throws IOException{
        byte[] bytes=new byte[64*1024];int count;
        while((count=input.read(bytes))!=-1){if(closed)throw new IOException("Session closed");output.write(bytes,0,count);}
    }
    static String safeName(String value){
        if(value==null)return "file";
        value=value.replace('\\','/');value=value.substring(value.lastIndexOf('/')+1).replaceAll("[\\p{Cntrl}:]","_");
        return value.isEmpty()||value.equals(".")||value.equals("..")||value.getBytes(StandardCharsets.UTF_8).length>240?"file":value;
    }
    // Called after native file handles have closed. Queue cleanup behind any provider I/O.
    void close(){closed=true;io.execute(()->remove(directory));io.shutdown();}
    private static void remove(File file){File[] children=file.listFiles();if(children!=null)for(File child:children)remove(child);file.delete();}
}
