const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const ts = require(process.argv[2]), root = process.argv[3];
const commands = [], events = [], timers = new Map(), intervals = new Map(); let timerId = 0, time = 100;
let preferences = new Map(), secrets = new Map(), records = new Map(), saves = [], platforms = [];
class Store {
  constructor(){this.directory='/private/mobile';}
  flag(k,f=false){return preferences.get(k)??f;} number(k,f){return preferences.get(k)??f;} text(k,f=''){return preferences.get(k)??f;}
  put(k,v){preferences.set(k,v);} static scope(s,p){return `${s.trim().toLowerCase().replace(/\.$/,'')}:${p}`;}
  async secret(k){return secrets.get(k)??'';} async saveSecret(k,v){secrets.set(k,v);} async removeSecret(k){secrets.delete(k);}
  async trustKey(){return 'fixture-device-key';}
  recent(s){return records.get(s)??[];} clearPreviews(){this.cleared=true;}
  async saveRecent(s,d,p){saves.push({scope:s,device:{...d},password:p});records.set(s,[d]);if(d.remember)secrets.set(`password:${s}:${d.id}`,p);}
  metadata(s,id,n,p){for(const d of this.recent(s))if(d.id===id){d.name=n;d.platform=p;}}
  async delete(s,id){records.set(s,this.recent(s).filter(d=>d.id!==id));secrets.delete(`password:${s}:${id}`);}
  async savePreview(){this.previewSaved=true;} discardRaw(){} discardOutgoing(){}
}
class Platform {
  constructor(c,end,release,feedback){this.context=c;this.end=end;this.release=release;platforms.push(this);this.choosing=false;}
  resume(){} async connected(){this.active=true;} async background(){this.release();}
  disconnect(){this.active=false;} dispose(){this.disconnect();} setMuted(){} cancelRequests(){} orientation(){} videoSize(){} async surface(){}
  async version(){this.versionCalls=(this.versionCalls??0)+1;return undefined;}
  async choose(){return undefined;} save(){} openLink(){}
}
const modules={};
function load(name){
  if(modules[name])return modules[name];const exports={}; modules[name]=exports;
  vm.runInNewContext(ts.transpileModule(fs.readFileSync(path.join(root,name+'.ets'),'utf8'),{
    compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2020,experimentalDecorators:true}
  }).outputText,{exports,console,Observed:c=>c,Date,Map,Set,Number,Math,JSON,Uint8Array,ArrayBuffer,
    setTimeout:fn=>{timers.set(++timerId,fn);return timerId;},clearTimeout:id=>timers.delete(id),
    setInterval:fn=>{intervals.set(++timerId,fn);return timerId;},clearInterval:id=>intervals.delete(id),
    require:name=>{
      if(name==='libcrossdesk.so')return {default:{mobileCommand:s=>commands.push(JSON.parse(s)),mobilePoll:()=>JSON.stringify(events.splice(0))}};
      if(name==='@kit.AbilityKit')return {bundleManager:{BundleFlag:{GET_BUNDLE_INFO_DEFAULT:0},getBundleInfoForSelfSync:()=>({versionName:'1.6.1'})}};
      if(name==='@kit.ArkUI')return {display:{getDefaultDisplaySync:()=>({width:1080,height:2400})},promptAction:{showToast(){}}};
      if(name==='@kit.ArkTS')return {util:{TextDecoder:{create:()=>({decodeWithStream:()=> 'Privacy policy'})},TextEncoder:class{encodeInto(s){return Buffer.from(s);}}}};
      if(name==='@kit.BasicServicesKit')return {systemDateTime:{TimeType:{STARTUP:0},getUptime:()=>time},pasteboard:{}};
      if(name==='./MobileStore')return {MobileStore:Store};
      if(name==='./MobilePlatform')return {MobilePlatform:Platform,MobileLifecycle:{}};
      if(name.startsWith('./'))return load(name.slice(2));throw Error(name);
    }});return exports;
}
const {MobileController}=load('MobileController');
const context={cacheDir:'/private/cache',filesDir:'/private',resourceManager:{getRawFileContentSync:()=>new Uint8Array([1])}};
const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
function feed(c,event){events.push({session:c.session,...event});c.poll();}
(async()=>{
  const c=new MobileController(context);await flush();assert.equal(commands.length,0);assert.equal(platforms[0].versionCalls,undefined);
  assert.equal(c.state.modal,'consent');await c.consent();await flush();
  assert(commands.some(c=>c.op==='start'));const start=commands.find(c=>c.op==='start');assert.equal(start.identity,'');
  feed(c,{type:'identity',identity:'123456789@secret'});await flush();
  assert.equal(secrets.get('identity:api.crossdesk.cn:9099'),'123456789@secret');
  feed(c,{type:'identity',identity:'123456789'});await flush();
  assert.equal(secrets.get('identity:api.crossdesk.cn:9099'),'123456789@secret'); // Public reconnect report cannot erase the password.
  feed(c,{type:'signal',status:1,controller:false,generation:1,id:'123456789'});
  assert.equal(c.state.signal,1);assert(commands.some(c=>c.op==='signal'&&c.message.type==='announcements_list'));
  await c.requestConnection('987654321');assert.equal(c.state.modal,'password');c.state.remember=true;c.begin('correct');
  const connect=commands.filter(c=>c.op==='connect').at(-1);assert.equal(connect.password,'correct');assert.equal(c.state.password,'');
  feed(c,{type:'data',connection:connect.connection,message:{type:3,host_info:{host_name:'Early desktop',platform:'windows',app_version:'1.6.1',supports_video_settings:true,displays:[{name:'Early display'}]}}});
  assert.equal(c.state.displays.length,1);assert(!commands.some(c=>c.op==='videoSettings'));
  feed(c,{type:'connection',connection:connect.connection,status:1});await flush();assert(c.state.ready);assert.equal(saves.length,1);assert.equal(saves[0].password,'correct');
  assert(commands.some(c=>c.op==='videoSettings'));assert.equal(saves[0].device.name,'Early desktop');
  feed(c,{type:'connection',connection:connect.connection,status:1});await flush();assert.equal(saves.length,1);
  feed(c,{type:'data',connection:connect.connection,message:{type:3,host_info:{host_name:'Desktop',platform:'windows',app_version:'1.6.1',supports_video_settings:true,displays:[{name:'Display1'}]}}});
  assert(c.video.supported);assert(commands.some(c=>c.op==='videoSettings'));assert.equal(c.state.displays.length,1);
  c.updateVideo(1,60);const settings=commands.filter(c=>c.op==='videoSettings').at(-1);
  feed(c,{type:'data',connection:connect.connection,message:{type:12,video_settings:{quality:settings.quality,frame_rate:60,preference:settings.preference,request_id:settings.request,accepted:false}}});
  assert.equal(c.state.videoRate,30);assert(c.video.failed);
  c.background();await flush();assert(c.state.ready);assert(!commands.slice(-2).some(c=>c.op==='disconnect'));assert(commands.some(c=>c.op==='release'));
  c.resume();await flush();c.end('');await c.requestConnection('987654321');
  assert.equal(c.state.modal,'connecting'); // Encrypted remembered password, no prompt.
  const second=commands.filter(c=>c.op==='connect').at(-1);
  feed(c,{type:'connection',connection:connect.connection,status:1});assert(!c.state.ready); // Superseded session.
  feed(c,{type:'connection',connection:second.connection,status:6});assert.equal(c.state.modal,'password');assert.match(c.state.notice,/密码错误/);
  const previousSession=c.session;c.saveSettings('private.example','9443');await flush();
  assert.equal(c.scope(),'private.example:9443');assert.equal(c.state.recent.length,0);
  feed(c,{session:previousSession,type:'identity',identity:'000000000@stale'});assert.notEqual(c.state.identity,'000000000');
  c.setPreviews(true);assert(c.state.previews);c.revoke();await flush();assert(!c.state.consent);assert(c.store.cleared);
  const calls=platforms[0].versionCalls;c.resume();await c.checkLocal(true);assert.equal(platforms[0].versionCalls,calls);
  c.destroy();assert.equal(intervals.size,0);
  console.log('PASS: consent-before-network, encrypted identity/password use, duplicate/stale callbacks, controller-only lifecycle, ACK rollback, background retention, server isolation and revocation');
})().catch(e=>{console.error(e);process.exitCode=1;});
