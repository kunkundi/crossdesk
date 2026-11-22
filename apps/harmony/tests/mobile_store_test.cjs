const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),crypto=require('node:crypto');
const ts=require(process.argv[2]),root=process.argv[3],exports_={};
const values=new Map(),secrets=new Map(),files=new Map(),opened=new Map();let nextFd=0,finishPacking,cacheMissing=false;
const Tag={ALIAS:1,SECRET:2,RETURN_TYPE:3,ACCESSIBILITY:4,SYNC_TYPE:5,CONFLICT_RESOLUTION:6};
const key=q=>Buffer.from(q.get(Tag.ALIAS)).toString();
const asset={Tag,ReturnType:{ALL:1},Accessibility:{DEVICE_FIRST_UNLOCKED:2},SyncType:{NEVER:0},ConflictResolution:{OVERWRITE:1},ErrorCode:{NOT_FOUND:404},
  query:async q=>{const v=secrets.get(key(q));if(!v)throw {code:404};return [v];},
  add:async q=>{assert.equal(q.get(Tag.ACCESSIBILITY),2);assert.equal(q.get(Tag.SYNC_TYPE),0);secrets.set(key(q),new Map(q));},
  remove:async q=>{secrets.delete(key(q));}};
const io={OpenMode:{READ_ONLY:1,WRITE_ONLY:2,CREATE:4,TRUNC:8},mkdirSync(){},
  accessSync:p=>{if(!files.has(p))throw Error('missing');return true;},
  listFileSync:p=>{if(cacheMissing)throw Error('cache removed');return [...files.keys()].filter(k=>k.startsWith(p+'/')).map(k=>k.slice(p.length+1));},
  unlinkSync:p=>files.delete(p),renameSync:(a,b)=>{assert(files.has(a));files.set(b,files.get(a));files.delete(a);},
  openSync:(p,mode)=>{if(mode&4)files.set(p,Buffer.alloc(0));if(!files.has(p))throw Error('missing');const file={fd:++nextFd};opened.set(file.fd,p);return file;},
  closeSync:f=>opened.delete(f.fd),readSync:(fd,b)=>{const content=files.get(opened.get(fd));new Uint8Array(b).set(content);return content.length;}};
vm.runInNewContext(ts.transpileModule(fs.readFileSync(path.join(root,'MobileStore.ets'),'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2020}}).outputText,
  {exports:exports_,Uint8Array,ArrayBuffer,Map,console,require:name=>{
    if(name==='./Localization')return {tr:text=>text};
    if(name==='libcrossdesk.so')return {default:{mobileGenerateTrustKey:()=>'fixture-device-key'}};
    if(name==='@kit.ArkData')return {preferences:{getPreferencesSync:()=>({getSync:(k,d)=>values.get(k)??d,putSync:(k,v)=>values.set(k,v),flushSync(){}})}};
    if(name==='@kit.AssetStoreKit')return {asset};
    if(name==='@kit.CryptoArchitectureKit')return {cryptoFramework:{createMd:()=>{const hash=crypto.createHash('sha256');return {update:async b=>hash.update(b.data),digest:async()=>({data:hash.digest()})};}}};
    if(name==='@kit.ArkTS')return {util:{TextEncoder:class{encodeInto(v){return Buffer.from(v);}},TextDecoder:{create:()=>({decodeWithStream:v=>Buffer.from(v).toString()})}}};
    if(name==='@kit.CoreFileKit')return {fileIo:io};
    if(name==='@kit.ImageKit')return {image:{PixelMapFormat:{RGBA_8888:1},createPixelMap:async()=>({release(){}}),createImagePacker:()=>({packToFile:()=>new Promise(r=>finishPacking=r),release(){}})}};
    throw Error(name);
  }});
const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
(async()=>{
  const Store=exports_.MobileStore,s=new Store({filesDir:'/private',cacheDir:'/cache'}),scope=Store.scope('EXAMPLE.COM.',9099);
  assert.equal(scope,'example.com:9099');
  await s.saveSecret('identity:'+scope,'123456789@private');assert.equal(await s.secret('identity:'+scope),'123456789@private');
  await s.saveRecent(scope,{id:'987654321',name:'Desktop',platform:'windows',remember:true,preview:''},'password');
  assert(!JSON.stringify([...values]).includes('password'));assert(!JSON.stringify([...values]).includes('@private'));
  assert.equal(await s.secret('password:'+scope+':987654321'),'password');
  s.put('consent',true);s.put('previews',true);
  let raw='/cache/mobile-previews/preview-1-1.rgba';files.set(raw,Buffer.alloc(640*360*4));
  let save=s.savePreview(scope,'987654321',raw);await flush();assert(finishPacking);s.clearPreviews();finishPacking();await save;
  assert.equal(files.size,0);assert.equal(s.recent(scope)[0].preview,'');assert.equal(opened.size,0);
  raw='/cache/mobile-previews/preview-2-1.rgba';files.set(raw,Buffer.alloc(640*360*4));
  save=s.savePreview(scope,'987654321',raw);await flush();finishPacking();await save;
  const preview=s.recent(scope)[0].preview;assert(preview.endsWith('.jpg'));assert(files.has(preview));assert(!files.has(raw));
  files.delete(preview);assert.equal(s.recent(scope).length,1);assert.equal(s.recent(scope)[0].preview,'');
  cacheMissing=true;assert.doesNotThrow(()=>s.clearPreviews());
  await s.delete(scope,'987654321');assert.equal(s.recent(scope).length,0);assert.equal(await s.secret('password:'+scope+':987654321'),'');
  console.log('PASS: Asset Store-only secrets without sync, private preview cache, asynchronous revocation, evicted cache and credential deletion');
})().catch(e=>{console.error(e);process.exitCode=1;});
