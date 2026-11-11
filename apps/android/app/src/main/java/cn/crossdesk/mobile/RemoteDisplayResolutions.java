package cn.crossdesk.mobile;

import org.json.JSONArray;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Host-confirmed desktop modes. Requests are scoped to the native display identity. */
final class RemoteDisplayResolutions {
    static final class Mode {
        final int width, height;
        Mode(int width,int height){this.width=width;this.height=height;}
        boolean same(Mode other){return other!=null&&width==other.width&&height==other.height;}
        String label(){return width+" × "+height;}
    }
    static final class Display {
        final int id;
        final String key;
        final Mode current;
        final List<Mode> modes=new ArrayList<>();
        Display(JSONObject value)throws Exception{
            id=(int)integer(value,"display_id",0,31);
            Object identity=value.get("display_key");
            if(!(identity instanceof String))throw new IllegalArgumentException();
            key=(String)identity;
            if(key.isEmpty()||key.getBytes(StandardCharsets.UTF_8).length>1024||key.indexOf('\0')>=0)throw new IllegalArgumentException();
            current=mode(value);
            JSONArray options=value.getJSONArray("modes");
            if(options.length()>256)throw new IllegalArgumentException();
            for(int i=0;i<options.length();i++){
                Mode item=mode(options.getJSONObject(i));
                if(!supports(item))modes.add(item);
            }
        }
        boolean supports(Mode value){for(Mode item:modes)if(item.same(value))return true;return false;}
    }
    static final class Request {
        final int displayId;
        final String displayKey;
        final Mode mode;
        final long id;
        Request(Display display,Mode mode,long id){this.displayId=display.id;this.displayKey=display.key;this.mode=mode;this.id=id;}
        boolean matches(JSONObject value)throws Exception{
            return integer(value,"request_id",1,0xffffffffL)==id&&integer(value,"display_id",0,31)==displayId
                    &&displayKey.equals(value.get("display_key"))&&mode.same(mode(value));
        }
    }
    private List<Display> displays=new ArrayList<>();
    Request requested;
    boolean pending,failed;
    Display display(int id){for(Display display:displays)if(display.id==id)return display;return null;}
    Request begin(int displayId,Mode mode){
        Display display=display(displayId);
        if(pending||display==null||!display.supports(mode)||display.current.same(mode))return null;
        long next=requested==null?1:(requested.id+1)&0xffffffffL;
        requested=new Request(display,mode,next==0?1:next);pending=true;failed=false;return requested;
    }
    void expire(long id){if(pending&&requested!=null&&requested.id==id){pending=false;failed=true;}}
    void receive(JSONObject message){
        try{
            int type=message.getInt("type");JSONArray values;JSONObject response=null;
            if(type==3){JSONObject host=message.getJSONObject("host_info");values=host.has("display_resolutions")?host.getJSONArray("display_resolutions"):new JSONArray();}
            else if(type==16){response=message.getJSONObject("display_resolution");if(requested==null||!requested.matches(response)||!(response.get("accepted") instanceof Boolean))return;values=message.getJSONArray("display_resolutions");}
            else return;
            if(values.length()>32)return;
            List<Display> actual=new ArrayList<>();
            for(int i=0;i<values.length();i++){
                Display display=new Display(values.getJSONObject(i));
                for(Display previous:actual)if(previous.id==display.id)return;
                actual.add(display);
            }
            displays=actual;
            if(response!=null){
                Display display=display(requested.displayId);pending=false;
                failed=!response.getBoolean("accepted")||display==null||!display.key.equals(requested.displayKey)||!display.current.same(requested.mode);
            }else if(pending){
                Display display=display(requested.displayId);
                if(display==null||!display.key.equals(requested.displayKey)||!display.supports(requested.mode))expire(requested.id);
            }
        }catch(Exception ignored){ /* Ignore malformed or unrelated control messages. */ }
    }
    String feedback(){return pending?L10n.tr("等待电脑端确认…"):failed?L10n.tr("调整失败，请重试。"):"";}
    private static Mode mode(JSONObject value)throws Exception{return new Mode((int)integer(value,"width",1,32768),(int)integer(value,"height",1,32768));}
    private static long integer(JSONObject value,String key,long min,long max)throws Exception{
        Object raw=value.get(key);
        if(!(raw instanceof Integer)&&!(raw instanceof Long))throw new IllegalArgumentException();
        long number=((Number)raw).longValue();if(number<min||number>max)throw new IllegalArgumentException();return number;
    }
}
