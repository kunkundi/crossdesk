package cn.crossdesk.mobile;

import android.content.Intent;
import android.net.Uri;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.URLSpan;
import android.view.View;
import android.widget.Toast;
import java.util.Locale;

/** Plain text plus HTTP(S) links, matching iOS; never interpret HTML or arbitrary Markdown. */
final class AnnouncementText {
    static CharSequence format(String body){
        String text=body.replace("\r\n","\n");SpannableStringBuilder result=new SpannableStringBuilder();
        for(int position=0;position<text.length();){
            if(text.charAt(position)=='['){
                int labelEnd=text.indexOf(']',position+1), start=labelEnd+2;
                if(labelEnd>position+1&&start<text.length()&&text.charAt(labelEnd+1)=='('&&
                    !text.substring(position+1,labelEnd).matches("(?s).*[\\[\\]\\r\\n].*")&&hasScheme(text,start)){
                    int end=urlEnd(text,start);
                    if(end<text.length()&&text.charAt(end)==')'&&
                        appendLink(result,text.substring(position+1,labelEnd),text.substring(start,end))){
                        position=end+1;continue;
                    }
                }
            }
            if(hasScheme(text,position)){
                int end=urlEnd(text,position);
                while(end>position&&".,;:!?".indexOf(text.charAt(end-1))>=0)end--;
                String address=text.substring(position,end);
                if(appendLink(result,address,address)){position=end;continue;}
            }
            result.append(text.charAt(position++));
        }
        return result;
    }
    static boolean isWebURL(String address){
        if(address==null)return false;
        for(int i=0;i<address.length();i++){char c=address.charAt(i);if(c<=0x20||c==0x7f||"\\<>\"".indexOf(c)>=0)return false;}
        Uri uri=Uri.parse(address);String scheme=uri.getScheme(),host=uri.getHost();
        return scheme!=null&&(scheme.equalsIgnoreCase("http")||scheme.equalsIgnoreCase("https"))&&host!=null&&!host.isEmpty();
    }
    private static boolean hasScheme(String text,int start){
        String prefix=text.substring(start,Math.min(text.length(),start+8)).toLowerCase(Locale.ROOT);
        return prefix.startsWith("https://")||prefix.startsWith("http://");
    }
    private static int urlEnd(String text,int start){
        int parentheses=0,brackets=0,end=start;
        for(;end<text.length();end++){
            char c=text.charAt(end);
            if(c<=0x20||c==0x7f||c=='\\'||c==96||"<>\"'{}，。；：！？、（）【】《》「」『』“”‘’".indexOf(c)>=0)break;
            if(c=='(')parentheses++;
            if(c==')'){if(parentheses==0)break;parentheses--;}
            if(c=='[')brackets++;
            if(c==']'){if(brackets==0)break;brackets--;}
        }
        return end;
    }
    private static boolean appendLink(SpannableStringBuilder result,String label,String destination){
        if(!isWebURL(destination))return false;
        int start=result.length();result.append(label);
        result.setSpan(new URLSpan(destination){
            @Override public void onClick(View widget){
                if(!isWebURL(getURL()))return;
                try{widget.getContext().startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse(getURL())).addCategory(Intent.CATEGORY_BROWSABLE));}
                catch(android.content.ActivityNotFoundException error){Toast.makeText(widget.getContext(),"没有可打开网页的浏览器",Toast.LENGTH_SHORT).show();}
            }
        },start,result.length(),Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        return true;
    }
}
