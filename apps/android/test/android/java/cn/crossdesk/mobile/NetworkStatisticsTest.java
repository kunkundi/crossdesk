package cn.crossdesk.mobile;

import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.*;

public class NetworkStatisticsTest {
    static JSONObject report(int mode,boolean srtp,double rtt)throws Exception{
        return new JSONObject().put("mode",mode).put("srtp",srtp).put("rtt",rtt).put("video",traffic(8000000,0,.02)).put("audio",traffic(64000,0,.01)).put("data",traffic(2000,3000,.03)).put("total",traffic(8066000,3000,.06));
    }
    private static JSONObject traffic(long in,long out,double loss)throws Exception{return new JSONObject().put("inbound",in).put("outbound",out).put("loss",loss);}

    @Test public void staleReportsAndRttExpireIndependently()throws Exception{
        RemoteNetworkStatistics state=new RemoteNetworkStatistics();state.receive(new RemoteNetworkStatistics.Report(report(0,true,20)),10);state.receive(new RemoteNetworkStatistics.Report(report(0,true,100)),11);assertEquals(40,state.snapshot(11).rtt,.0001);
        for(double invalid:new double[]{-1,2001})state.receive(new RemoteNetworkStatistics.Report(report(0,true,invalid)),12);
        assertEquals(40,state.snapshot(13.99).rtt,.0001);assertTrue(Double.isNaN(state.snapshot(14).rtt));assertNotNull(state.snapshot(14).report);assertNull(state.snapshot(15).report);
        state.receive(new RemoteNetworkStatistics.Report(report(0,true,0)),16);assertEquals(0,state.snapshot(16).rtt,0);
    }
    @Test public void submittedFramesAreCountedOnceAndIdleVideoExpires(){
        RemoteNetworkStatistics state=new RemoteNetworkStatistics();state.frame(1,100,1920,1080,10);state.frame(1,100,1920,1080,10.1);state.frame(2,200,1920,1080,10.2);
        assertEquals(2,state.snapshot(10.3).fps);assertEquals(150,state.snapshot(10.3).latency,.0001);assertEquals("1920 × 1080",state.snapshot(10.3).resolution());assertEquals(1,state.snapshot(11).fps);assertEquals(0,state.snapshot(11.3).fps);assertTrue(Double.isNaN(state.snapshot(11.3).latency));
    }

}
