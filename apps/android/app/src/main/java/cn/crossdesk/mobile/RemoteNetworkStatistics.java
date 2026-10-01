package cn.crossdesk.mobile;

import org.json.JSONObject;
import java.util.ArrayDeque;
import java.util.Locale;

/** Monotonic, thread-safe measurements; the UI samples these once a second, as on iOS. */
final class RemoteNetworkStatistics {
    static final class Traffic {
        final long inbound,outbound;final double loss;
        Traffic(JSONObject json){inbound=json.optLong("inbound",-1);outbound=json.optLong("outbound",-1);loss=json.optDouble("loss",Double.NaN);}
    }
    static final class Report {
        final Traffic[] traffic=new Traffic[4];final int mode;final boolean srtp;final double rtt;
        Report(JSONObject json){
            String[] keys={"video","audio","data","total"};
            for(int i=0;i<keys.length;i++){JSONObject item=json.optJSONObject(keys[i]);traffic[i]=item==null?null:new Traffic(item);}
            mode=json.optInt("mode",2);srtp=json.optBoolean("srtp");rtt=json.optDouble("rtt",Double.NaN);
        }
    }
    static final class Snapshot {
        final Report report;final int fps,width,height;final double latency,rtt;
        Snapshot(Report report,int fps,int width,int height,double latency,double rtt){this.report=report;this.fps=fps;this.width=width;this.height=height;this.latency=latency;this.rtt=rtt;}
        String mode(){return report==null?"—":report.mode==0?"P2P 直连":report.mode==1?"TURN 中继":"—";}
        String resolution(){return width>0&&height>0?width+" × "+height:"—";}
        String summary(){
            String value=mode()+(width>0&&height>0?" · "+width+"×"+height:"")+" · "+bitrate(report==null||report.traffic[3]==null?-1:report.traffic[3].inbound);
            if(report!=null&&report.traffic[0]!=null&&report.traffic[0].loss>0)value+=" · 丢包 "+loss(report.traffic[0].loss);return value;
        }
    }
    private static final class Frame {
        final double time,latency;Frame(double time,double latency){this.time=time;this.latency=latency;}
    }
    private Report report;
    private double reportTime,rttTime,averageRtt=Double.NaN;
    private long lastFrameId;
    private int width,height;
    private final ArrayDeque<Frame> frames=new ArrayDeque<>();
    static double now(){return System.nanoTime()/1_000_000_000.0;}
    synchronized void receive(Report value,double now){
        report=value;reportTime=now;
        if(!Double.isFinite(value.rtt)||value.rtt<0||value.rtt>2000)return;
        averageRtt=Double.isFinite(averageRtt)&&now-rttTime<3?averageRtt+.25*(value.rtt-averageRtt):value.rtt;rttTime=now;
    }
    synchronized void frame(long id,double latency,int width,int height,double now){
        if(id<=lastFrameId||width<=0||height<=0)return;lastFrameId=id;this.width=width;this.height=height;
        // Native code validates clock calibration before adding local decode/render waits.
        // A valid but slow frame must not disappear just because its delay exceeds 5 s.
        prune(now);frames.addLast(new Frame(now,Double.isFinite(latency)&&latency>=0?latency:Double.NaN));
        while(frames.size()>240)frames.removeFirst();
    }
    synchronized void resetVideo(){frames.clear();width=height=0;}
    synchronized Snapshot snapshot(double now){
        prune(now);double total=0;int count=0;
        for(Frame frame:frames)if(Double.isFinite(frame.latency)){total+=frame.latency;count++;}
        return new Snapshot(now-reportTime<3?report:null,frames.size(),width,height,count==0?Double.NaN:total/count,now-rttTime<3?averageRtt:Double.NaN);
    }
    private void prune(double now){while(!frames.isEmpty()&&now-frames.peekFirst().time>=1)frames.removeFirst();}
    static String bitrate(long value){return value<0?"—":value>=1_000_000?String.format(Locale.ROOT,"%.1f Mbps",value/1_000_000.0):value>=1000?String.format(Locale.ROOT,"%.0f Kbps",value/1000.0):value+" bps";}
    static String loss(double value){return Double.isFinite(value)&&value>=0?String.format(Locale.ROOT,"%.1f%%",value*100):"—";}
    static String latency(double value){return !Double.isFinite(value)||value<0?"—":value<1?"<1 ms":String.format(Locale.ROOT,"%.0f ms",value);}
}
