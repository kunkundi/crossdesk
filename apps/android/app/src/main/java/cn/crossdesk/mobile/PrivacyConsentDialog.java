package cn.crossdesk.mobile;

import android.app.Activity;
import android.app.Dialog;
import android.view.Gravity;
import android.view.Window;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.TextView;

import static cn.crossdesk.mobile.MobileUi.*;

/** Explicit agreement is required; refusal dismisses only the current visit. */
final class PrivacyConsentDialog extends Dialog {
    PrivacyConsentDialog(Activity activity,MobileUi ui,Runnable accepted){
        this(activity,ui,new PrivacyPolicyDocument(activity),accepted);
    }
    PrivacyConsentDialog(Activity activity,MobileUi ui,PrivacyPolicyDocument policy,Runnable accepted){
        super(activity);requestWindowFeature(Window.FEATURE_NO_TITLE);setCancelable(false);setCanceledOnTouchOutside(false);
        LinearLayout sheet=ui.column();sheet.setBackground(ui.background(android.graphics.Color.WHITE,20));sheet.setClipToOutline(true);
        TextView title=ui.text("隐私政策",17,true);title.setGravity(Gravity.CENTER);sheet.addView(title,ui.size(-1,56));ui.divider(sheet);
        sheet.addView(policy.view(ui),new LinearLayout.LayoutParams(-1,0,1));ui.divider(sheet);
        LinearLayout actions=ui.column();actions.setPadding(ui.dp(20),ui.dp(12),ui.dp(20),ui.dp(12));sheet.addView(actions);
        CheckBox hasReadPolicy=new CheckBox(activity);hasReadPolicy.setText("我已阅读并同意《隐私政策》");hasReadPolicy.setTextColor(INK);hasReadPolicy.setTextSize(14);hasReadPolicy.setMinHeight(ui.dp(44));actions.addView(hasReadPolicy,ui.size(-1,-2));ui.gap(actions,12);
        TextView agree=ui.primary("同意并继续",()->{
            if(!hasReadPolicy.isChecked()||!policy.available)return;
            dismiss();accepted.run();
        });agree.setEnabled(false);agree.setAlpha(.45f);actions.addView(agree,ui.size(-1,48));
        hasReadPolicy.setOnCheckedChangeListener((button,checked)->{boolean enabled=checked&&policy.available;agree.setEnabled(enabled);agree.setAlpha(enabled?1:.45f);});
        ui.gap(actions,12);actions.addView(ui.action("暂不同意",SECONDARY,this::dismiss),ui.size(-1,44));
        setContentView(sheet);Window window=getWindow();if(window!=null){window.setBackgroundDrawableResource(android.R.color.transparent);window.setDimAmount(.22f);}
    }
    @Override public void show(){
        super.show();Window window=getWindow();if(window!=null){window.setLayout(-1,Math.round(getContext().getResources().getDisplayMetrics().heightPixels*.9f));window.setGravity(Gravity.BOTTOM);}
    }
}
