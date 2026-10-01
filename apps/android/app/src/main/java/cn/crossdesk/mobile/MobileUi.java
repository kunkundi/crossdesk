package cn.crossdesk.mobile;

import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.view.*;
import android.widget.*;

/** Shared visual tokens matching the SwiftUI mobile screens. Values are in dp/sp. */
final class MobileUi {
    static final int BACKGROUND=0xFFF2F2F7, INK=0xFF1C1C1E, SECONDARY=0xFF737378,
            LINE=0xFFE2E2E7, BLUE=0xFF2E7ADB, RED=0xFFFF3B30, GREEN=0xFF248A3D;
    // Match the iOS 26 Form sections and segmented Picker dimensions.
    static final int SETTINGS_CARD_RADIUS=26, SEGMENT_HEIGHT=32, SEGMENT_INSET=2;
    final Context context;
    MobileUi(Context context) { this.context=context; }
    int dp(float value) { return Math.round(value*context.getResources().getDisplayMetrics().density); }
    GradientDrawable background(int color,int radius) {
        GradientDrawable shape=new GradientDrawable();shape.setColor(color);shape.setCornerRadius(dp(radius));return shape;
    }
    void card(View view,int radius) { GradientDrawable shape=background(Color.WHITE,radius);shape.setStroke(dp(.5f),LINE);view.setBackground(shape);view.setClipToOutline(true); }
    FrameLayout shadowCard(View content,int radius) {
        card(content,radius);
        FrameLayout wrapper=new FrameLayout(context);
        wrapper.setClipChildren(false);wrapper.setClipToPadding(false);
        wrapper.setBackground(new CardShadow(dp(radius),dp(14),dp(5)));
        wrapper.addView(content,new FrameLayout.LayoutParams(-1,-2));
        return wrapper;
    }
    LinearLayout column() { LinearLayout v=new LinearLayout(context);v.setOrientation(LinearLayout.VERTICAL);return v; }
    LinearLayout row() { LinearLayout v=new LinearLayout(context);v.setGravity(Gravity.CENTER_VERTICAL);return v; }
    TextView text(String title,int size,boolean bold) {
        TextView v=new TextView(context);v.setText(title);v.setTextSize(size);v.setTextColor(INK);v.setIncludeFontPadding(false);
        if(bold)v.setTypeface(null,Typeface.BOLD);return v;
    }
    TextView action(String title,int color,Runnable click) {
        TextView v=text(title,16,true);v.setGravity(Gravity.CENTER);v.setTextColor(color);
        v.setMinHeight(dp(48));v.setClickable(true);v.setFocusable(true);v.setOnClickListener(ignored->click.run());
        v.setAccessibilityDelegate(new View.AccessibilityDelegate(){@Override public void onInitializeAccessibilityNodeInfo(View host,android.view.accessibility.AccessibilityNodeInfo info){super.onInitializeAccessibilityNodeInfo(host,info);info.setClassName("android.widget.Button");}});
        return v;
    }
    TextView primary(String title,Runnable click) {
        TextView v=action(title,Color.WHITE,click);
        GradientDrawable gradient=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{BLUE,0xFF1F61C7});gradient.setCornerRadius(dp(12));v.setBackground(gradient);return v;
    }
    View iconButton(String icon,String label,Runnable click) {
        FrameLayout box=new FrameLayout(context);box.setBackground(background(Color.WHITE,10));
        Icon glyph=new Icon(context,icon,INK);FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(dp(22),dp(22),Gravity.CENTER);box.addView(glyph,p);
        box.setContentDescription(label);box.setClickable(true);box.setFocusable(true);box.setOnClickListener(v->click.run());return box;
    }
    View icon(String name,int color) { return new Icon(context,name,color); }
    void gap(LinearLayout parent,int height){parent.addView(new View(context),new LinearLayout.LayoutParams(1,dp(height)));}
    void divider(LinearLayout parent){View line=new View(context);line.setBackgroundColor(LINE);parent.addView(line,new LinearLayout.LayoutParams(-1,dp(.5f)));}
    LinearLayout.LayoutParams size(int width,int height){return new LinearLayout.LayoutParams(width<0?width:dp(width),height<0?height:dp(height));}
    /** The iOS connection panel uses black at 5.5%, blur 14, y 5, rather than elevation. */
    private static final class CardShadow extends Drawable {
        private final Paint bitmapPaint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        private final float cornerRadius,blurRadius,offsetY;
        private final int inset;
        private Bitmap bitmap;
        CardShadow(float cornerRadius,float blurRadius,float offsetY) {
            this.cornerRadius=cornerRadius;this.blurRadius=blurRadius;this.offsetY=offsetY;
            inset=(int)Math.ceil(blurRadius*2+Math.abs(offsetY));
        }
        @Override protected void onBoundsChange(Rect bounds) {
            bitmap=null;
            if(bounds.isEmpty())return;
            // Render once on a bitmap canvas: shadow layers need software rendering on API 26.
            bitmap=Bitmap.createBitmap(bounds.width()+inset*2,bounds.height()+inset*2,Bitmap.Config.ARGB_8888);
            bitmap.setDensity(Bitmap.DENSITY_NONE);
            Paint shadowPaint=new Paint(Paint.ANTI_ALIAS_FLAG);
            shadowPaint.setColor(Color.WHITE);
            shadowPaint.setShadowLayer(blurRadius,0,offsetY,Color.argb(Math.round(255*.055f),0,0,0));
            new Canvas(bitmap).drawRoundRect(inset,inset,inset+bounds.width(),inset+bounds.height(),cornerRadius,cornerRadius,shadowPaint);
        }
        @Override public void draw(Canvas canvas) {
            if(bitmap!=null)canvas.drawBitmap(bitmap,getBounds().left-inset,getBounds().top-inset,bitmapPaint);
        }
        @Override public void setAlpha(int alpha) { bitmapPaint.setAlpha(alpha);invalidateSelf(); }
        @Override public void setColorFilter(ColorFilter filter) { bitmapPaint.setColorFilter(filter);invalidateSelf(); }
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }
    @android.annotation.SuppressLint("ViewConstructor") // Programmatic icon primitive, not inflated from XML.
    static final class Icon extends View {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG); private final String name;
        Icon(Context context,String name,int color){super(context);this.name=name;paint.setColor(color);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
        @Override protected void onDraw(Canvas c){
            c.save();c.scale(getWidth()/24f,getHeight()/24f);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.7f);paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);
            switch(name){
                case "settings":
                    c.drawCircle(12,12,7,paint);c.drawCircle(12,12,3,paint);
                    for(int i=0;i<8;i++){c.save();c.rotate(i*45,12,12);c.drawLine(12,2,12,5,paint);c.restore();}break;
                case "bell":
                    path(c,5,17,6,14,6,9,7,6,10,4,14,4,17,6,18,9,18,14,19,17,5,17);c.drawArc(9,17,15,22,0,180,false,paint);c.drawLine(12,2,12,4,paint);break;
                case "history":
                    c.drawArc(4,4,21,21,215,315,false,paint);path(c,3,4,3,10,9,10);path(c,12,7,12,13,16,15);break;
                case "back":path(c,15,4,7,12,15,20);break;
                case "refresh":c.drawArc(4,4,20,20,35,310,false,paint);path(c,20,3,20,9,14,9);break;
                case "close":path(c,6,6,18,18);path(c,18,6,6,18);break;
                case "arrow":path(c,4,12,20,12);path(c,14,6,20,12,14,18);break;
                case "keyboard":
                    c.drawRoundRect(2,5,22,19,3,3,paint);for(int y=9;y<=12;y+=3)for(int x=6;x<=18;x+=4)c.drawPoint(x,y,paint);c.drawLine(7,16,17,16,paint);break;
                case "display":c.drawRoundRect(2,3,22,17,2,2,paint);path(c,12,17,12,21);path(c,7,21,17,21);break;
                case "sliders":
                    for(int y=6;y<=18;y+=6){c.drawLine(3,y,21,y,paint);c.drawCircle(y==12?15:8,y,2,paint);}break;
                case "audio":path(c,3,9,7,9,12,4,12,20,7,15,3,15,3,9);c.drawArc(10,5,21,19,-65,130,false,paint);break;
                case "mouse":c.drawRoundRect(5,2,19,22,7,7,paint);path(c,12,3,12,10);break;
                case "folder":path(c,2,19,2,5,9,5,12,8,22,8,22,19,2,19);break;
                case "chart":path(c,3,3,3,21,22,21);path(c,7,17,7,13);path(c,12,17,12,8);path(c,18,17,18,4);break;
                case "lock":c.drawRoundRect(5,10,19,22,2,2,paint);c.drawArc(8,2,16,15,180,180,false,paint);c.drawPoint(12,16,paint);break;
                case "key":
                    paint.setStyle(Paint.Style.FILL);c.drawCircle(7,8,5,paint);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(4);path(c,10,11,19,20);path(c,15,16,18,13);path(c,18,19,21,16);
                    paint.setStyle(Paint.Style.FILL);int keyColor=paint.getColor();paint.setColor(Color.WHITE);c.drawCircle(6,7,1.5f,paint);paint.setColor(keyColor);break;
                case "unlock":c.drawRoundRect(5,10,19,22,2,2,paint);c.drawArc(8,2,16,15,180,150,false,paint);c.drawPoint(12,16,paint);break;
                case "clipboard":c.drawRoundRect(4,4,20,22,2,2,paint);c.drawRoundRect(8,2,16,7,2,2,paint);break;
                case "eye":path(c,2,12,6,7,12,5,18,7,22,12,18,17,12,19,6,17,2,12);c.drawCircle(12,12,3,paint);break;
                default:c.drawCircle(12,12,9,paint);path(c,12,10,12,17);c.drawPoint(12,6,paint);
            }c.restore();
        }
        private void path(Canvas c,float... values){Path p=new Path();p.moveTo(values[0],values[1]);for(int i=2;i<values.length;i+=2)p.lineTo(values[i],values[i+1]);c.drawPath(p,paint);}
    }
}
