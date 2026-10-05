package cn.crossdesk.mobile;

import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;

/** Reflows the existing panels, keeping text, focus and scroll state during window resizing. */
@android.annotation.SuppressLint("ViewConstructor")
final class HomeLayout extends ScrollView {
    final LinearLayout panels;
    private final MobileUi ui;
    private int panelHeight;

    HomeLayout(MobileUi ui){
        super(ui.context);this.ui=ui;
        setFillViewport(true);setClipChildren(false);setClipToPadding(false);
        panels=new LinearLayout(ui.context){
            @Override protected void onMeasure(int widthSpec,int heightSpec){
                // ScrollView offers unbounded height even with an explicit LayoutParams height.
                super.onMeasure(widthSpec,MeasureSpec.makeMeasureSpec(panelHeight,MeasureSpec.EXACTLY));
            }
        };panels.setGravity(Gravity.TOP);
        panels.setPadding(ui.dp(20),ui.dp(24),ui.dp(20),ui.dp(12));
        panels.setClipChildren(false);panels.setClipToPadding(false);
        addView(panels,new LayoutParams(-1,-1));
    }

    @Override protected void onMeasure(int widthSpec,int heightSpec){
        boolean wide=MeasureSpec.getSize(widthSpec)>=ui.dp(900);
        panels.setOrientation(wide?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);
        // A short split window or the IME can leave less room than the two panels need.
        // Keep the recent list usable and let the outer viewport scroll in that case.
        panelHeight=Math.max(ui.dp(wide?360:440),MeasureSpec.getSize(heightSpec));
        if(panels.getChildCount()==2){
            View connection=panels.getChildAt(0),recent=panels.getChildAt(1);
            LinearLayout.LayoutParams first=(LinearLayout.LayoutParams)connection.getLayoutParams();
            first.width=wide?ui.dp(360):-1;first.height=-2;first.weight=0;first.gravity=Gravity.TOP;
            LinearLayout.LayoutParams second=(LinearLayout.LayoutParams)recent.getLayoutParams();
            second.width=wide?0:-1;second.height=wide?-1:0;second.weight=1;
            second.topMargin=wide?0:ui.dp(20);second.setMarginStart(wide?ui.dp(24):0);
            second.resolveLayoutDirection(panels.getLayoutDirection());
        }
        super.onMeasure(widthSpec,heightSpec);
    }
}
