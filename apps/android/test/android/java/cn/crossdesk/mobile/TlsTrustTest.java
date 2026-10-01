package cn.crossdesk.mobile;

import android.content.Context;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.io.*;
import java.lang.reflect.*;
import java.net.InetAddress;
import java.nio.file.Files;
import java.security.KeyStore;
import java.util.concurrent.*;
import javax.net.ssl.*;
import static org.junit.Assert.*;

/** Exercises the actual MiniRTC TLS path inside an Android app process (AT_SECURE=1).
 * The bundled PKCS12 keys are public test fixtures, only served on loopback. No
 * websocket upgrade/login or external server is involved in these tests. */
@RunWith(AndroidJUnit4.class)
public class TlsTrustTest {
    @Test public void explicitlyTrustedCertificateCompletesHandshake() throws Exception {
        assertTrue("The app must load the supplied trust bundle despite AT_SECURE", handshake("valid",true));
    }
    @Test public void untrustedCertificateIsRejected() throws Exception {
        assertFalse("A test CA absent from the system store must not be trusted",handshake("valid",false));
    }
    @Test public void certificateForAnotherHostIsRejected() throws Exception {
        assertFalse("Trusting the CA must not bypass hostname verification",handshake("wrong-host",true));
    }
    private boolean handshake(String certificate,boolean trustFixture) throws Exception {
        var instrumentation=InstrumentationRegistry.getInstrumentation();Context context=instrumentation.getTargetContext();
        KeyStore keyStore=KeyStore.getInstance("PKCS12");char[] password="local-test-only".toCharArray();
        try(InputStream input=instrumentation.getContext().getAssets().open("tls/"+certificate+".p12")){keyStore.load(input,password);}
        KeyManagerFactory keys=KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());keys.init(keyStore,password);
        SSLContext tls=SSLContext.getInstance("TLS");tls.init(keys.getKeyManagers(),null,null);
        ExecutorService serverWorker=Executors.newSingleThreadExecutor();
        NativeSession session=new NativeSession(context,"127.0.0.1",1,"unused","unused",new NativeSession.Listener(){
            public void status(String value){} public void connected(){fail("Test server never opens a desktop session");}
            public void ended(String value){} public void data(JSONObject value){}public void videoSize(int w,int h){}
            public void clipboard(String value){}public void statistics(RemoteNetworkStatistics.Snapshot value){}
        });
        File roots=new File(context.getCacheDir(),"tls-test-roots.pem");
        Field rtcField=NativeSession.class.getDeclaredField("RTC");rtcField.setAccessible(true);ExecutorService rtc=(ExecutorService)rtcField.get(null);
        try(SSLServerSocket server=(SSLServerSocket)tls.getServerSocketFactory().createServerSocket(0,1,InetAddress.getByName("127.0.0.1"))){
            server.setSoTimeout(10000);
            if(trustFixture){try(InputStream input=instrumentation.getContext().getAssets().open("tls/root.pem")){Files.copy(input,roots.toPath(),java.nio.file.StandardCopyOption.REPLACE_EXISTING);}}
            else{Method export=NativeSession.class.getDeclaredMethod("exportTrust");export.setAccessible(true);File systemRoots=(File)export.invoke(session);Files.copy(systemRoots.toPath(),roots.toPath(),java.nio.file.StandardCopyOption.REPLACE_EXISTING);}
            Future<Boolean> request=serverWorker.submit(()->{
                try(SSLSocket socket=(SSLSocket)server.accept()){
                    socket.setSoTimeout(10000);socket.startHandshake();
                    String first=new BufferedReader(new InputStreamReader(socket.getInputStream(),java.nio.charset.StandardCharsets.US_ASCII)).readLine();
                    return first!=null&&first.startsWith("GET ");
                }catch(SSLException expected){return false;}
            });
            Method create=NativeSession.class.getDeclaredMethod("nCreate",NativeSession.class,String.class,int.class,String.class,String.class,String.class);create.setAccessible(true);
            Field handle=NativeSession.class.getDeclaredField("handle");handle.setAccessible(true);
            rtc.submit(()->{try{long value=(long)create.invoke(null,session,"127.0.0.1",server.getLocalPort(),"",context.getCacheDir().getAbsolutePath(),roots.getAbsolutePath());handle.setLong(session,value);assertNotEquals(0L,value);}catch(ReflectiveOperationException error){throw new RuntimeException(error);}}).get(10,TimeUnit.SECONDS);
            return request.get(15,TimeUnit.SECONDS);
        }finally{session.close();rtc.submit(()->{}).get(10,TimeUnit.SECONDS);serverWorker.shutdownNow();roots.delete();}
    }
}
