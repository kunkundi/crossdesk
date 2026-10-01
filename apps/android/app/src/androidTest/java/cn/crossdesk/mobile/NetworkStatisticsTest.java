package cn.crossdesk.mobile;

import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.*;

public class NetworkStatisticsTest {
    static JSONObject report(int mode,boolean srtp,double rtt)throws Exception{
        return new JSONObject().put("mode",mode).put("srtp",srtp).put("rtt",rtt).put("video",traffic(8000000,0,.02)).put("audio",traffic(64000,0,.01)).put("data",traffic(2000,3000,.03)).put("total",traffic(8066000,3000,.06));
    }
    private static JSONObject traffic(long in,long out,double loss)throws Exception{return new JSONObject().put("inbound",in).put("outbound",out).put("loss",loss);}
    @Test public void channelRatesRouteAndEncryptionComeFromTransport()throws Exception{
        RemoteNetworkStatistics state=new RemoteNetworkStatistics();assertNull(state.snapshot(10).report);assertEquals("—",state.snapshot(10).mode());
        state.receive(new RemoteNetworkStatistics.Report(report(1,true,20)),10);var snapshot=state.snapshot(10);
        assertEquals(8000000,snapshot.report.traffic[0].inbound);assertEquals(64000,snapshot.report.traffic[1].inbound);assertEquals(3000,snapshot.report.traffic[2].outbound);assertEquals(8066000,snapshot.report.traffic[3].inbound);assertEquals(.06,snapshot.report.traffic[3].loss,.0001);assertEquals("TURN 中继",snapshot.mode());assertTrue(snapshot.report.srtp);
        state.receive(new RemoteNetworkStatistics.Report(report(0,false,20)),11);assertEquals("P2P 直连",state.snapshot(11).mode());assertFalse(state.snapshot(11).report.srtp);
    }
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
    @Test public void displaySwitchResetsVideoWithoutLosingTraffic()throws Exception{
        RemoteNetworkStatistics state=new RemoteNetworkStatistics();state.receive(new RemoteNetworkStatistics.Report(report(1,true,20)),10);state.frame(1,100,1920,1080,10);state.resetVideo();state.frame(1,100,1920,1080,10.1);
        assertEquals(0,state.snapshot(10.1).fps);assertEquals("—",state.snapshot(10.1).resolution());assertNotNull(state.snapshot(10.1).report);
        state.frame(2,-1,1280,720,10.2);state.frame(3,Double.NaN,1280,720,10.3);state.frame(4,Double.POSITIVE_INFINITY,1280,720,10.4);assertEquals(3,state.snapshot(10.5).fps);assertTrue(Double.isNaN(state.snapshot(10.5).latency));
        state.frame(5,50,1280,720,10.6);assertEquals(50,state.snapshot(10.6).latency,0);assertEquals("1280 × 720",state.snapshot(10.6).resolution());
    }
    @Test public void slowFramesKeepTheirMeasuredLatencyInsteadOfBecomingUnknown(){
        RemoteNetworkStatistics state=new RemoteNetworkStatistics();state.frame(1,6000,1920,1080,10);state.frame(2,8000,1920,1080,10.2);
        assertEquals(7000,state.snapshot(10.3).latency,0);assertEquals("7000 ms",RemoteNetworkStatistics.latency(state.snapshot(10.3).latency));
        state.frame(3,50,1920,1080,11.3);assertEquals(50,state.snapshot(11.3).latency,0);
    }
    @Test public void formatMatchesIosAndUnknownIsNotZero(){
        assertEquals("—",RemoteNetworkStatistics.bitrate(-1));assertEquals("0 bps",RemoteNetworkStatistics.bitrate(0));assertEquals("64 Kbps",RemoteNetworkStatistics.bitrate(64000));assertEquals("8.0 Mbps",RemoteNetworkStatistics.bitrate(8000000));
        assertEquals("<1 ms",RemoteNetworkStatistics.latency(0));assertEquals("—",RemoteNetworkStatistics.latency(Double.NaN));assertEquals("0.5%",RemoteNetworkStatistics.loss(.005));assertEquals("120.0%",RemoteNetworkStatistics.loss(1.2));
    }
}
