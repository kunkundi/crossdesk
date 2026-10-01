package cn.crossdesk.mobile;

import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.text.TextUtils;
import android.text.method.LinkMovementMethod;
import android.view.*;
import android.widget.*;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.function.Consumer;
import static cn.crossdesk.mobile.MobileUi.*;

/** Grouped announcement list and plain-text detail, sharing the iOS presentation. */
@android.annotation.SuppressLint("ViewConstructor")
final class AnnouncementView extends ScrollView {
    private final MobileUi ui;
    private final AnnouncementInbox inbox;
    private final LinearLayout content;
    private final Consumer<AnnouncementInbox.Item> open;
    private final Runnable authorize;
    private final boolean detail;
    private final long id,revision;
    private final SimpleDateFormat dates=new SimpleDateFormat("yyyy-MM-dd HH:mm",Locale.getDefault());
    private TextView countLabel;
    private AlertDialog deleteDialog;
    private float downX,downY;
    private boolean pulling,canPull;
    private final int slop;

    AnnouncementView(MobileUi ui,AnnouncementInbox inbox,AnnouncementInbox.Item selected,
                     Consumer<AnnouncementInbox.Item> open,Runnable authorize){
        super(ui.context);this.ui=ui;this.inbox=inbox;this.open=open;this.authorize=authorize;
        detail=selected!=null;id=detail?selected.id:0;revision=detail?selected.revision:0;
        slop=ViewConfiguration.get(getContext()).getScaledTouchSlop();
        setFillViewport(true);setScrollBarStyle(SCROLLBARS_OUTSIDE_OVERLAY);
        content=ui.column();content.setPadding(ui.dp(20),ui.dp(detail?20:16),ui.dp(20),ui.dp(28));
        addView(content,new ScrollView.LayoutParams(-1,-2));
        setOnScrollChangeListener((v,x,y,oldX,oldY)->{if(y>oldY)loadVisiblePage();});
        render();
    }
    void render(){
        int y=getScrollY();content.removeAllViews();countLabel=null;
        if(detail)renderDetail();else renderList();
        post(()->{if(isAttachedToWindow()){scrollTo(0,y);loadVisiblePage();}});
    }
    private String date(AnnouncementInbox.Item item){return dates.format(new Date(item.updatedAt*1000));}
    private TextView message(String value,int color){
        TextView text=ui.text(value,14,false);text.setTextColor(color);text.setLineSpacing(ui.dp(3),1);return text;
    }
    private void renderList(){
        if(inbox.deleteFailed){content.addView(message("无法删除这条公告，请重试。",RED));ui.gap(content,12);}
        if(!inbox.isConnected()){
            content.addView(message("连接服务器后可获取最新公告。",SECONDARY));
            content.addView(ui.action("连接服务器",BLUE,authorize));ui.gap(content,8);
        }else if(inbox.failed){
            LinearLayout error=ui.row();error.addView(message("公告加载失败，请重试。",SECONDARY),new LinearLayout.LayoutParams(0,-2,1));
            error.addView(ui.action("重试",BLUE,inbox::refresh),ui.size(64,48));content.addView(error);ui.gap(content,8);
        }
        countLabel=ui.text("共 "+inbox.total+" 条 · "+inbox.unread+" 条未读",13,false);
        countLabel.setTextColor(SECONDARY);countLabel.setPadding(ui.dp(16),ui.dp(8),0,ui.dp(8));content.addView(countLabel);
        LinearLayout card=ui.column();ui.card(card,12);content.addView(card);
        for(AnnouncementInbox.Item item:inbox.items){
            if(card.getChildCount()>0)ui.divider(card);
            card.addView(itemRow(item),ui.size(-1,-2));
        }
        if(inbox.loading){
            LinearLayout loading=ui.row();loading.setGravity(Gravity.CENTER);loading.setPadding(0,ui.dp(18),0,ui.dp(18));
            ProgressBar spinner=new ProgressBar(getContext());spinner.setIndeterminateTintList(ColorStateList.valueOf(BLUE));loading.addView(spinner,ui.size(20,20));
            TextView label=message("加载中…",SECONDARY);label.setPadding(ui.dp(10),0,0,0);loading.addView(label);card.addView(loading);
        }else if(inbox.items.size()<inbox.total&&!inbox.failed){
            TextView more=ui.action("加载更多",BLUE,inbox::loadMore);more.setEnabled(inbox.isConnected());card.addView(more,ui.size(-1,48));
        }else if(inbox.loaded&&inbox.total==0&&!inbox.failed&&inbox.isConnected()){
            TextView empty=message("暂无公告",SECONDARY);empty.setGravity(Gravity.CENTER);card.addView(empty,ui.size(-1,120));
        }
    }
    private View itemRow(AnnouncementInbox.Item item){
        LinearLayout row=ui.row();row.setBackgroundColor(Color.WHITE);row.setPadding(ui.dp(16),ui.dp(16),ui.dp(12),ui.dp(16));
        LinearLayout words=ui.column();
        LinearLayout top=ui.row();top.setGravity(Gravity.TOP);
        View dot=new View(getContext());dot.setBackground(ui.background(item.read?Color.TRANSPARENT:BLUE,4));
        LinearLayout.LayoutParams dotLayout=ui.size(7,7);dotLayout.topMargin=ui.dp(7);dotLayout.rightMargin=ui.dp(10);top.addView(dot,dotLayout);
        TextView title=ui.text(item.title,16,!item.read);title.setMaxLines(2);title.setEllipsize(TextUtils.TruncateAt.END);
        words.addView(title,ui.size(-1,-2));ui.gap(words,7);TextView date=ui.text(date(item),12,false);date.setTextColor(SECONDARY);words.addView(date);
        top.addView(words,new LinearLayout.LayoutParams(0,-2,1));row.addView(top,new LinearLayout.LayoutParams(0,-2,1));
        TextView chevron=ui.text("›",23,false);chevron.setTextColor(0xFFB7B7BD);chevron.setPadding(ui.dp(10),0,0,0);row.addView(chevron);
        row.setContentDescription(item.title+"，"+date(item)+"，"+(item.read?"已读":"未读"));row.setFocusable(true);
        row.setOnClickListener(v->open.accept(item));row.setOnLongClickListener(v->{confirmDelete(item);return true;});
        return new SwipeRow(row,()->confirmDelete(item));
    }
    private void confirmDelete(AnnouncementInbox.Item item){
        if(deleteDialog!=null)deleteDialog.dismiss();
        deleteDialog=new AlertDialog.Builder(getContext()).setTitle("删除公告").setMessage("删除这条公告？")
            .setNegativeButton("取消",null).setPositiveButton("删除",(dialog,which)->inbox.dismiss(item)).show();
    }
    private void renderDetail(){
        AnnouncementInbox.Item item=inbox.find(id,revision);
        if(item==null){content.addView(message("这条公告已更新或删除，请返回公告列表查看。",SECONDARY));return;}
        content.addView(ui.text(item.title,22,true));ui.gap(content,16);
        TextView date=ui.text(date(item)+" · "+(item.read?"已读":"未读"),12,false);date.setTextColor(SECONDARY);content.addView(date);ui.gap(content,16);
        if(inbox.readSaveFailed){
            content.addView(message("无法保存本地阅读状态，请重试。",RED));
            content.addView(ui.action("重试保存阅读状态",BLUE,()->inbox.markRead(item)));ui.gap(content,16);
        }
        TextView body=ui.text("",17,false);body.setText(AnnouncementText.format(item.body));body.setTextIsSelectable(true);
        body.setMovementMethod(LinkMovementMethod.getInstance());body.setLinksClickable(true);body.setLinkTextColor(BLUE);body.setLineSpacing(ui.dp(4),1);
        content.addView(body,ui.size(-1,-2));
    }
    private void loadVisiblePage(){
        if(!detail&&isAttachedToWindow()&&inbox.isConnected()&&!inbox.loading&&!inbox.failed&&inbox.items.size()<inbox.total&&
            content.getHeight()>0&&getHeight()>0&&getScrollY()+getHeight()>=content.getHeight()-ui.dp(48))inbox.loadMore();
    }
    private void trackPull(MotionEvent event){
        if(!detail&&inbox.isConnected()&&!inbox.loading){
            if(event.getActionMasked()==MotionEvent.ACTION_DOWN){downX=event.getX();downY=event.getY();pulling=false;canPull=getScrollY()==0;}
            else if(event.getActionMasked()==MotionEvent.ACTION_MOVE&&canPull&&getScrollY()==0&&
                event.getY()-downY>slop&&event.getY()-downY>Math.abs(event.getX()-downX))pulling=true;
        }
    }
    @Override public boolean onInterceptTouchEvent(MotionEvent event){
        trackPull(event);return pulling||super.onInterceptTouchEvent(event);
    }
    @android.annotation.SuppressLint("ClickableViewAccessibility") // Pull gesture only; toolbar refresh is the accessible action.
    @Override public boolean onTouchEvent(MotionEvent event){
        trackPull(event);
        if(!pulling)return super.onTouchEvent(event);
        boolean reached=event.getY()-downY>=ui.dp(72);
        if(event.getActionMasked()==MotionEvent.ACTION_MOVE&&countLabel!=null)countLabel.setText(reached?"松开刷新":"下拉刷新");
        if(event.getActionMasked()==MotionEvent.ACTION_UP||event.getActionMasked()==MotionEvent.ACTION_CANCEL){
            pulling=false;if(reached&&event.getActionMasked()==MotionEvent.ACTION_UP)inbox.refresh();else render();
        }
        return true;
    }
    @Override protected void onDetachedFromWindow(){if(deleteDialog!=null)deleteDialog.dismiss();super.onDetachedFromWindow();}

    /** Reveal a delete action without interfering with vertical scrolling or row taps. */
    private final class SwipeRow extends FrameLayout {
        private final View front,remove;
        private float x,y,origin;
        private boolean dragging;
        SwipeRow(View front,Runnable delete){
            super(ui.context);this.front=front;setClipChildren(true);
            remove=ui.action("删除",Color.WHITE,delete);remove.setBackgroundColor(RED);
            remove.setVisibility(INVISIBLE);
            remove.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);remove.setFocusable(false);
            addView(remove,new FrameLayout.LayoutParams(ui.dp(76),-1,Gravity.END));addView(front,new FrameLayout.LayoutParams(-1,-2));
        }
        @Override public boolean onInterceptTouchEvent(MotionEvent event){
            if(event.getActionMasked()==MotionEvent.ACTION_DOWN){front.animate().cancel();x=event.getX();y=event.getY();origin=front.getTranslationX();dragging=false;}
            if(event.getActionMasked()==MotionEvent.ACTION_MOVE&&Math.abs(event.getX()-x)>slop&&Math.abs(event.getX()-x)>Math.abs(event.getY()-y)){
                dragging=true;remove.setVisibility(VISIBLE);getParent().requestDisallowInterceptTouchEvent(true);return true;
            }
            return super.onInterceptTouchEvent(event);
        }
        @android.annotation.SuppressLint("ClickableViewAccessibility") // Taps/long presses belong to the accessible row and delete button.
        @Override public boolean onTouchEvent(MotionEvent event){
            if(!dragging)return super.onTouchEvent(event);
            int direction=getLayoutDirection()==LAYOUT_DIRECTION_RTL?1:-1;
            if(event.getActionMasked()==MotionEvent.ACTION_MOVE)front.setTranslationX(direction*Math.max(0,Math.min(ui.dp(76),direction*(origin+event.getX()-x))));
            if(event.getActionMasked()==MotionEvent.ACTION_UP||event.getActionMasked()==MotionEvent.ACTION_CANCEL){
                boolean expanded=Math.abs(front.getTranslationX())>ui.dp(38);dragging=false;
                front.animate().translationX(expanded?direction*ui.dp(76):0).setDuration(150).withEndAction(()->{if(!expanded)remove.setVisibility(INVISIBLE);}).start();
                remove.setFocusable(expanded);remove.setImportantForAccessibility(expanded?IMPORTANT_FOR_ACCESSIBILITY_YES:IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
            }
            return true;
        }
    }
}
