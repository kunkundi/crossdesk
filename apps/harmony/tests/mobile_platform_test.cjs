const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const ts=require(process.argv[2]),root=process.argv[3], exports_={};
const textExports={};
vm.runInNewContext(ts.transpileModule(fs.readFileSync(path.join(root,'AnnouncementText.ets'),'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2020}}).outputText,{exports:textExports});
let agentResolve, startResolve, pipResolve, creating=0, activeTasks=0, calls=[], ended=[], releases=0;
const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
const pip={setAutoStartEnabled:v=>calls.push(['auto',v]),on:(type,fn)=>pip[type]=fn,updatePiPControlStatus:(t,s)=>calls.push(['mute',s]),
  updateContentSize(){},startPiP:async()=>calls.push(['pip']),stopPiP:async()=>calls.push(['stopPip'])};
const background={BackgroundMode:{DATA_TRANSFER:1},startBackgroundRunning:()=>new Promise(r=>{calls.push(['start']);startResolve=()=>{activeTasks++;r();};}),
  stopBackgroundRunning:async()=>{calls.push(['stop']);activeTasks--;}};
vm.runInNewContext(ts.transpileModule(fs.readFileSync(path.join(root,'MobilePlatform.ets'),'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2020}}).outputText,
  {exports:exports_,console,Promise,Date,Math,require:name=>{
    if(name==='./Localization')return {tr:text=>text};
    if(name==='./AnnouncementText')return textExports;
    if(name==='@kit.BasicServicesKit')return {deviceInfo:{deviceType:'phone'}};
    if(name==='@kit.AbilityKit')return {wantAgent:{OperationType:{START_ABILITY:1},WantAgentFlags:{UPDATE_PRESENT_FLAG:1},getWantAgent:()=>new Promise(r=>agentResolve=r)}};
    if(name==='@kit.BackgroundTasksKit')return {backgroundTaskManager:background};
    if(name==='@kit.CoreFileKit')return {picker:{},fileIo:{},fileUri:{}};
    if(name==='@kit.NetworkKit')return {http:{}};
    if(name==='@kit.ArkUI')return {display:{getDefaultDisplaySync:()=>({densityPixels:1})},window:{Orientation:{PORTRAIT:1,AUTO_ROTATION:2},
      getLastWindow:async()=>({setWindowKeepScreenOn:async()=>{},getWindowProperties:()=>({windowRect:{width:360}}),setPreferredOrientation:async()=>{}})},
      PiPWindow:{PiPTemplateType:{VIDEO_LIVE:1},VideoLiveControlGroup:{MUTE_SWITCH:402},PiPControlType:{MUTE_SWITCH:8},PiPControlStatus:{PAUSE:0,PLAY:1},
        PiPState:{ABOUT_TO_START:1,STARTED:2,ABOUT_TO_RESTORE:3,STOPPED:4},isPiPEnabled:()=>true,create:()=>{creating++;return new Promise(r=>pipResolve=r);}}};
    throw Error(name);
  }});
(async()=>{
  const p=new exports_.MobilePlatform({abilityInfo:{name:'Entry',bundleName:'test'}},r=>ended.push(r),()=>releases++,()=>{});
  const first=p.connected();await flush();agentResolve({});await flush();
  // Disconnect during system start, then reconnect before it resolves.
  p.disconnect();const second=p.connected();await flush();assert.equal(calls.filter(c=>c[0]==='start').length,1);
  startResolve();await flush();assert.equal(activeTasks,0);agentResolve({});await flush();startResolve();await second;assert.equal(activeTasks,1);
  const a=p.surface({},640,360), b=p.surface({},640,360);assert.equal(creating,1);pipResolve(pip);await Promise.all([a,b]);
  let muted=false;p.onMute=v=>muted=v;pip.controlEvent({controlType:8,status:0});assert(muted);p.setMuted(true);assert(calls.some(c=>c[0]==='mute'&&c[1]===0));
  p.setChoosing(true);await p.background();assert(!calls.some(c=>c[0]==='pip'));assert.equal(ended.length,0);
  p.resume();p.setChoosing(false);await p.background();assert(calls.some(c=>c[0]==='pip'));assert(releases>0);
  pip.stateChange(2,'');assert(p.pipActive);p.resume();await flush();assert(!p.pipActive);assert.equal(ended.length,0);
  assert(calls.some(c=>c[0]==='stopPip'));await p.background();pip.stateChange(2,'');pip.stateChange(4,'close');assert.equal(ended.length,1);
  p.disconnect();await flush();assert.equal(activeTasks,0);
  console.log('PASS: rapid reconnect serializes background tasks, PiP creation deduplicates, mute control, picker guard, background release and close cleanup');
})().catch(e=>{console.error(e);process.exitCode=1;});
