package cn.crossdesk.mobile;

import android.content.Context;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.IOException;

import static cn.crossdesk.mobile.MobileUi.*;

/** The same bundled policy as iOS, readable before any network consent. */
final class PrivacyPolicyDocument {
    final boolean available;
    final String text;

    PrivacyPolicyDocument(Context context){this(load(context));}
    PrivacyPolicyDocument(String document){
        available=document!=null&&!document.trim().isEmpty();
        String value=available?document:"隐私政策暂时无法读取，请拒绝联网并重新安装应用。";
        int start=value.indexOf("## 中文\n"),end=start<0?-1:value.indexOf("## English",start);
        text=start>=0&&end>start?value.substring(start+"## 中文\n".length(),end).trim():value;
    }
    private static String load(Context context){
        try{return LicenseCatalog.read(context,"PRIVACY.md");}catch(IOException error){return null;}
    }
    ScrollView view(MobileUi ui){
        ScrollView scroll=new ScrollView(ui.context);scroll.setBackgroundColor(android.graphics.Color.WHITE);
        LinearLayout content=ui.column();content.setPadding(ui.dp(20),ui.dp(20),ui.dp(20),ui.dp(20));scroll.addView(content);
        for(String paragraph:text.split("\n\n")){
            paragraph=paragraph.trim();if(paragraph.isEmpty()||paragraph.equals("---"))continue;
            boolean heading=paragraph.startsWith("#"),date=paragraph.startsWith("更新日期：");
            String value=heading?paragraph.replaceFirst("^#+\\s*",""):paragraph.replace("**","");
            TextView label=ui.text(value,date?12:16,heading);label.setTextIsSelectable(true);label.setLineSpacing(ui.dp(4),1);
            if(date)label.setTextColor(SECONDARY);
            LinearLayout.LayoutParams spacing=new LinearLayout.LayoutParams(-1,-2);spacing.bottomMargin=ui.dp(14);content.addView(label,spacing);
        }
        return scroll;
    }
}
