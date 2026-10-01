package cn.crossdesk.mobile;

import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CheckBox;
import android.widget.TextView;
import androidx.lifecycle.Lifecycle;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.util.Map;

import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class PrivacyConsentTest {
    private SharedPreferences preferences;
    private Map<String,?> saved;

    @Before public void prepare(){
        Context context=InstrumentationRegistry.getInstrumentation().getTargetContext();
        preferences=context.getSharedPreferences("settings",Context.MODE_PRIVATE);saved=preferences.getAll();
        preferences.edit().remove("networkConsent").putString("host","127.0.0.1").putInt("port",1).commit();
    }
    @After public void restore(){
        SharedPreferences.Editor edit=preferences.edit();
        for(String key:new String[]{"networkConsent","host","port"}){
            edit.remove(key);Object value=saved.get(key);
            if(value instanceof Boolean)edit.putBoolean(key,(Boolean)value);
            else if(value instanceof String)edit.putString(key,(String)value);
            else if(value instanceof Integer)edit.putInt(key,(Integer)value);
        }
        edit.commit();
    }
    private static Object field(MainActivity activity,String name){
        try{var field=MainActivity.class.getDeclaredField(name);field.setAccessible(true);return field.get(activity);}
        catch(Exception error){throw new AssertionError(error);}
    }
    private static Dialog notice(MainActivity activity){
        Dialog dialog=(Dialog)field(activity,"privacyDialog");assertNotNull("An unconsented visit must prompt",dialog);assertTrue(dialog.isShowing());return dialog;
    }
    private static TextView findText(View view,String text){
        if(view instanceof TextView&&((TextView)view).getText().toString().equals(text))return (TextView)view;
        if(view instanceof ViewGroup){ViewGroup parent=(ViewGroup)view;for(int i=0;i<parent.getChildCount();i++){TextView found=findText(parent.getChildAt(i),text);if(found!=null)return found;}}
        return null;
    }
    private static TextView action(Dialog dialog,String text){
        TextView view=findText(dialog.getWindow().getDecorView(),text);assertNotNull(text,view);return view;
    }
    private static void decline(MainActivity activity){action(notice(activity),"暂不同意").performClick();}
    private static void agree(MainActivity activity){
        Dialog dialog=notice(activity);action(dialog,"我已阅读并同意《隐私政策》").performClick();action(dialog,"同意并继续").performClick();
    }

    @Test public void coldLaunchRequiresExplicitAgreementBeforeNetworking(){
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
            scenario.onActivity(activity->{
                Dialog dialog=notice(activity);
                assertFalse(((CheckBox)action(dialog,"我已阅读并同意《隐私政策》")).isChecked());
                assertFalse(action(dialog,"同意并继续").isEnabled());
                action(dialog,"同意并继续").performClick();
                assertTrue(dialog.isShowing());assertFalse(preferences.getBoolean("networkConsent",false));
                assertNull(field(activity,"signaling"));
                decline(activity);assertFalse(dialog.isShowing());assertNull(field(activity,"signaling"));
                assertNotNull(findText(activity.getWindow().getDecorView(),"等待隐私授权"));
            });
            scenario.onActivity(activity->assertNull(field(activity,"privacyDialog")));
        }
    }
    @Test public void refusalPromptsOnBackgroundReturnButNotTransientInactivity(){
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
            scenario.onActivity(PrivacyConsentTest::decline);
            scenario.moveToState(Lifecycle.State.STARTED).moveToState(Lifecycle.State.RESUMED);
            scenario.onActivity(activity->{assertNull(field(activity,"privacyDialog"));assertNull(field(activity,"signaling"));});
            for(int visit=0;visit<3;visit++){
                scenario.moveToState(Lifecycle.State.CREATED).moveToState(Lifecycle.State.RESUMED);
                scenario.onActivity(activity->{notice(activity);assertNull(field(activity,"signaling"));decline(activity);});
            }
        }
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
            scenario.onActivity(activity->{notice(activity);assertNull(field(activity,"signaling"));});
        }
    }
    @Test public void backgroundDismissesOldDialogAndClearsTheCheckbox(){
        Dialog[] original={null};
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
            scenario.onActivity(activity->{original[0]=notice(activity);action(original[0],"我已阅读并同意《隐私政策》").performClick();assertTrue(action(original[0],"同意并继续").isEnabled());});
            scenario.moveToState(Lifecycle.State.CREATED);
            assertFalse(original[0].isShowing());
            scenario.moveToState(Lifecycle.State.RESUMED);
            scenario.onActivity(activity->{Dialog next=notice(activity);assertNotSame(original[0],next);assertFalse(action(next,"同意并继续").isEnabled());assertNull(field(activity,"signaling"));});
        }
    }
    @Test public void agreementPersistsAndStopsAutomaticPrompts(){
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
            scenario.onActivity(activity->{Dialog dialog=notice(activity);agree(activity);assertTrue(preferences.getBoolean("networkConsent",false));assertFalse(dialog.isShowing());assertNotNull(field(activity,"signaling"));});
            scenario.onActivity(activity->assertNull(field(activity,"privacyDialog")));
            scenario.moveToState(Lifecycle.State.CREATED).moveToState(Lifecycle.State.RESUMED);
            scenario.onActivity(activity->assertNull(field(activity,"privacyDialog")));
        }
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
            scenario.onActivity(activity->assertNull(field(activity,"privacyDialog")));
            scenario.moveToState(Lifecycle.State.CREATED);
            preferences.edit().putBoolean("networkConsent",false).commit();
            scenario.moveToState(Lifecycle.State.RESUMED);
            scenario.onActivity(activity->{notice(activity);assertNull(field(activity,"signaling"));});
        }
    }
    @Test public void existingLocalRecordsDoNotImplyConsent(){
        Context context=InstrumentationRegistry.getInstrumentation().getTargetContext();
        SharedPreferences identities=context.getSharedPreferences("identities",Context.MODE_PRIVATE);
        String key="privacy-upgrade.invalid:1";
        try{
            identities.edit().putString(key,"existing-device-identity").commit();
            try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
                scenario.onActivity(activity->{notice(activity);assertNull(field(activity,"signaling"));});
            }
        }finally{identities.edit().remove(key).commit();}
    }
    @Test public void bundledChinesePolicyIsOfflineAndMissingPolicyCannotAuthorize(){
        Context context=InstrumentationRegistry.getInstrumentation().getTargetContext();
        PrivacyPolicyDocument policy=new PrivacyPolicyDocument(context);
        assertTrue(policy.available);assertTrue(policy.text.contains("更新日期："));assertTrue(policy.text.contains("查询与删除请求"));assertFalse(policy.text.contains("## English"));
        assertFalse(new PrivacyPolicyDocument(" \n").available);
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)){
            scenario.onActivity(activity->{
                decline(activity);int[] accepted={0};
                Dialog missing=new PrivacyConsentDialog(activity,new MobileUi(activity),new PrivacyPolicyDocument((String)null),()->accepted[0]++);
                try{
                    missing.show();action(missing,"我已阅读并同意《隐私政策》").performClick();
                    assertFalse(action(missing,"同意并继续").isEnabled());action(missing,"同意并继续").performClick();
                    assertEquals(0,accepted[0]);assertFalse(preferences.getBoolean("networkConsent",false));assertNull(field(activity,"signaling"));
                    action(missing,"暂不同意").performClick();assertFalse(missing.isShowing());
                }finally{missing.dismiss();}
            });
        }
    }
}
