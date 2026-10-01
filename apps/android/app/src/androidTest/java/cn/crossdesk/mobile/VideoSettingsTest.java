package cn.crossdesk.mobile;

import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.*;

public class VideoSettingsTest {
    private final RemoteVideoSettings.Values first=new RemoteVideoSettings.Values(1,30,2),second=new RemoteVideoSettings.Values(0,60,1);
    private void same(RemoteVideoSettings.Values expected,RemoteVideoSettings.Values actual){assertEquals(expected.quality,actual.quality);assertEquals(expected.frameRate,actual.frameRate);assertEquals(expected.preference,actual.preference);}
    @Test public void supportedHostsAndProtocolValuesOnly(){
        RemoteVideoSettings state=new RemoteVideoSettings(0);assertEquals(-1,state.begin(first));same(new RemoteVideoSettings.Values(1,30,0),state.selection);state.setSupported(true);
        for(int q=-1;q<4;q++)for(int fps:new int[]{0,24,30,60,120})for(int p=-1;p<4;p++)assertEquals(q>=0&&q<=2&&(fps==30||fps==60)&&p>=0&&p<=2,state.begin(new RemoteVideoSettings.Values(q,fps,p))>=0);
    }
    @Test public void acknowledgementCommitsAndRejectionRollsBack(){
        RemoteVideoSettings state=new RemoteVideoSettings(1);state.setSupported(true);long id=state.begin(first);assertTrue(state.pending);same(new RemoteVideoSettings.Values(1,30,1),state.applied);
        state.receive(first,id,true);same(first,state.selection);assertFalse(state.pending);assertFalse(state.failed);state.expire(id);assertFalse(state.failed);
        id=state.begin(second);state.receive(second,id,false);same(first,state.selection);assertTrue(state.failed);state.begin(second);assertFalse(state.failed);
    }
    @Test public void rapidChangesUseNewestSelectionAndLatestAcknowledgedRollback(){
        RemoteVideoSettings state=new RemoteVideoSettings(1);state.setSupported(true);long a=state.begin(first),b=state.begin(second);
        state.receive(first,a,true);state.expire(a);same(second,state.selection);assertTrue(state.pending);
        state.receive(second,b,false);same(first,state.selection);assertTrue(state.failed);
        long c=state.begin(second);state.receive(second,c,true);state.receive(first,a,true);state.receive(first,c+1,true);same(second,state.selection);same(second,state.applied);
    }
    @Test public void timeoutLateReplyAndCapabilityLoss(){
        RemoteVideoSettings state=new RemoteVideoSettings(1);state.setSupported(true);long id=state.begin(first);state.expire(id);assertTrue(state.failed);same(new RemoteVideoSettings.Values(1,30,1),state.selection);
        state.receive(first,id,true);same(first,state.selection);assertFalse(state.failed);
        id=state.begin(second);state.setSupported(false);state.receive(second,id,true);state.expire(id);same(first,state.selection);assertFalse(state.failed);assertFalse(state.pending);
    }
    @Test public void malformedAndOutOfRangeAcknowledgementsCannotChangeState()throws Exception{
        RemoteVideoSettings state=new RemoteVideoSettings(1);state.setSupported(true);long id=state.begin(first);
        for(Object bad:new Object[]{-1L,0x100000000L,1.5,"1"}){JSONObject response=first.status(id,true);response.getJSONObject("video_settings").put("request_id",bad);state.receive(response);assertTrue(state.pending);}
        JSONObject response=first.status(id,true);response.getJSONObject("video_settings").put("quality",0x100000001L);state.receive(response);assertTrue(state.pending);
        response=first.status(id,true);response.getJSONObject("video_settings").put("accepted","true");state.receive(response);assertTrue(state.pending);
        state.receive(first.status(id,true));assertFalse(state.pending);same(first,state.applied);
    }
    @Test public void requestIdsWrapAsUnsigned32Bit(){
        RemoteVideoSettings state=new RemoteVideoSettings(1);state.setSupported(true);state.requestId=state.appliedRequestId=0xfffffffeL;
        long a=state.begin(first),b=state.begin(second);assertEquals(0xffffffffL,a);assertEquals(0,b);
        state.receive(first,a,true);same(second,state.selection);state.receive(second,b,true);state.receive(first,a,true);same(second,state.applied);
    }
    @Test public void softwareDecodeStartsWithinDisplayBudgetAndAllowsManualOverrides(){
        for(int edge:new int[]{640,720,1280})same(new RemoteVideoSettings.Values(0,30,1),new RemoteVideoSettings(1,edge).selection);
        for(int edge:new int[]{0,1281,1920,3840})same(new RemoteVideoSettings.Values(1,30,2),new RemoteVideoSettings(2,edge).selection);
        RemoteVideoSettings state=new RemoteVideoSettings(-1,640);same(new RemoteVideoSettings.Values(0,30,1),state.selection);
        state.setSupported(true);RemoteVideoSettings.Values high=new RemoteVideoSettings.Values(2,60,0);long id=state.begin(high);
        state.receive(high,id,true);same(high,state.selection);
    }
}
