'use strict';
// Private one-shot local PlayerIns experiment, exact target only. Native creation
// and positioning occur on the already observed creator update thread/context.
const config = __CONFIG__;
const main = Process.mainModule, base = main.base;
const norm = s=>s.replace(/\//g,'\\').toLowerCase();
if (Process.id!==config.pid || Process.arch!=='x64' || Process.platform!=='windows' ||
    main.size!==0x42d2000 || norm(main.path)!==norm(config.exe) || config.action!=='spawn_one' ||
    config.input.pid!==config.pid || config.input.new_manipulator_kind!==0)
  throw new Error('Exact one-shot target/action identity mismatch');
const required = [0xc0f7a0,0xa49460,0xa07960,0xa05a80,0xa217a0,0x7e3220,
  0x9f0870,0x9eeb40,0x9f6320,0x7de3f0,0xbc2c50,0xbf4bf0];
if (config.anchors.length!==required.length || new Set(config.anchors.map(x=>x.rva)).size!==required.length ||
    !required.every(rva=>config.anchors.some(x=>x.rva===rva))) throw new Error('Native anchor scope mismatch');
let listener=null, stopped=false, pending=false, errors=0, calls=0, ticks=0, stage='armed';
let world=null, primary=null, actor=null, originalHeader=null, createdAt=0, nativeThread=null;
let heapBytes=0, moduleBytes=0, targetPosition=null, result=null;
const owned=[];
function inImage(p,n=1) { return p.compare(base)>=0 && p.add(n).compare(base.add(main.size))<=0; }
function page(p,n,executable=false) {
  if (p.isNull() || n<1 || n>256 || p.compare(ptr('0x10000'))<0 || p.add(n).compare(ptr('0x800000000000'))>0)
    throw new Error('Invalid bounded address');
  let at=0;
  while(at<n) {
    const here=p.add(at),range=Process.findRangeByAddress(here);
    if(!range || !range.protection.includes('r') || (executable&&!range.protection.includes('x')))
      throw new Error('Selected page lacks required access');
    const chunk=Math.min(n-at,range.base.add(range.size).sub(here).toUInt32());
    if(chunk<1)throw new Error('Empty selected page');
    at+=chunk;
  }
}
function read(p,n) {
  page(p,n); const module=inImage(p,n);
  if(module) { if(moduleBytes+n>32768)throw new Error('Module read budget');moduleBytes+=n; }
  else { if(heapBytes+n>32768)throw new Error('Selected heap read budget');heapBytes+=n; }
  return p.readByteArray(n);
}
function q(p) {
  const b=new DataView(read(p,8));
  return ptr('0x'+b.getUint32(4,true).toString(16).padStart(8,'0')+b.getUint32(0,true).toString(16).padStart(8,'0'));
}
function u(p) { const bytes=read(p,4);return new DataView(bytes).getUint32(0,true); }
function type(p) {
  const vt=q(p); if(!inImage(vt.sub(8),16))throw new Error('Vtable is outside selected image');
  const col=q(vt.sub(8));if(!inImage(col,24))throw new Error('COL outside image');
  const raw=new DataView(read(col,24));
  if(raw.getUint32(0,true)!==1 || raw.getUint32(4,true)!==0 || !col.equals(base.add(raw.getUint32(20,true))))
    throw new Error('COL signature/adjustment/self mismatch');
  const td=base.add(raw.getUint32(12,true));if(!inImage(td,144))throw new Error('RTTI descriptor outside image');
  const bytes=new Uint8Array(read(td.add(16),128)),end=bytes.indexOf(0);
  if(end<1)throw new Error('RTTI name is unavailable');
  return {name:String.fromCharCode(...bytes.slice(0,end)),vtable:vt};
}
function requireType(p,name,rva) {
  const t=type(p);if(t.name!==name || (rva!==undefined&&!t.vtable.equals(base.add(rva))))throw new Error('Selected native object type mismatch');
  return t;
}
function allocation(bytes) {
  const raw=Memory.alloc(bytes+16);owned.push(raw);
  const aligned=raw.add(15).and(ptr('0xfffffffffffffff0'));
  aligned.writeByteArray(new Uint8Array(bytes));return aligned;
}
function floats(p) { return [0,4,8,12].map(o=>p.add(o).readFloat()); }
function validVector(v) { return v.every(Number.isFinite)&&v.slice(0,3).every(n=>Math.abs(n)<1e7)&&Math.abs(v[3])<=2; }
const options={abi:'win64',scheduling:'cooperative',exceptions:'propagate'};
const create = new NativeFunction(base.add(0xa49460),'pointer',['pointer','uint','pointer'],options);
const getPosition = new NativeFunction(base.add(0x9f0870),'pointer',['pointer','pointer'],options);
const getForward = new NativeFunction(base.add(0x9eeb40),'pointer',['pointer','pointer'],options);
const setPosition = new NativeFunction(base.add(0x9f6320),'void',['pointer','pointer'],options);
const positionBuffer=allocation(16),forwardBuffer=allocation(16),placement=allocation(16),params=allocation(48);
function modelReady(p) {
  const component=q(p.add(0x50)),root=q(p.add(0x1ff8));
  if(component.isNull()||root.isNull())return false;
  if(!q(component.add(0x10)).equals(p))throw new Error('Actor component owner differs');
  const node=q(root.add(0x68));if(node.isNull())return false;
  page(node.add(0x80),32);return true;
}
function pose(p) {
  if(!modelReady(p))throw new Error('Selected actor components not ready');
  getPosition(p,positionBuffer);const v=floats(positionBuffer);
  if(!validVector(v))throw new Error('Nonfinite/out-of-range actor position');return v;
}
function finish(reason) {
  if(stopped)return;stopped=true;
  let detached=true;try{if(listener!==null)listener.detach();}catch(_){errors++;detached=false;}
  send({type:'trace_end',pid:Process.id,reason,callback_errors:errors,listeners_detached:detached,
    native_creation_calls:calls,ticks,stage,selected_heap_bytes:heapBytes,module_bytes:moduleBytes,result,
    own_parameter_memory_only:true,existing_primary_modified_by_script:false});
}
function schedule(reason) { if(!pending){pending=true;setImmediate(()=>finish(reason));} }
function header(m) {
  const h=read(m.add(0x40),16),d=new DataView(h),count=d.getInt32(0,true),array=q(m.add(0x48));
  if(count!==config.input.count || count<2 || count>8 || !array.equals(ptr(config.input.array)))throw new Error('Player collection changed');
  return {count,array};
}
function before(m,creator) {
  if(!m.equals(ptr(config.input.manager)))throw new Error('World incarnation changed');
  requireType(m,'.?AVWorldChrManImp@NS_SPRJ@@',0x2a30058);
  const p=q(m.add(0x88));if(!p.equals(ptr(config.input.primary)))throw new Error('Primary changed');
  requireType(p,'.?AVPlayerIns@NS_SPRJ@@',0x2a2b338);
  if(!creator.equals(q(m.add(0x3148))))throw new Error('Not the selected creator callback');
  requireType(creator,'.?AVCSDebugChrCreator@NS_SPRJ@@',0x2a952a8);
  if(u(creator.add(0x40))!==0 || u(creator.add(0x44))!==0)throw new Error('Debug creator is busy');
  const h=header(m);if(!q(h.array).equals(p))throw new Error('Primary registry link changed');
  for(let i=1;i<h.count;i++)if(!q(h.array.add(i*0x38)).isNull())throw new Error('Additional player already exists');
  const argument=u(p.add(0x70)),fieldHandle=u(p.add(0x78));
  if(argument!==config.input.spawn_argument || fieldHandle!==config.input.primary_field_handle || fieldHandle>0xffff)
    throw new Error('Creation argument source changed');
  const canonical=fieldHandle===0xffff?0xffffffff:fieldHandle&0x7ff;
  if(canonical!==config.input.canonical_config_value)throw new Error('Canonical field parameter changed');
  const dataOwner=q(base.add(0x3d67408)),sourceOwner=q(base.add(0x3d5aac0));
  if(!dataOwner.equals(ptr(config.input.data_owner)) || !sourceOwner.equals(ptr(config.input.source_owner)))
    throw new Error('Source owner changed');
  const data=dataOwner.add(0x31e0),source=q(sourceOwner.add(8));
  if(!data.equals(q(p.add(0x21f8))) || !source.equals(q(p.add(0x2000))) || source.isNull())
    throw new Error('Live parameter relationships disagree with native constructor');
  requireType(q(p.add(0x58)),'.?AVPadManipulator@NS_SPRJ@@',0x29d4b30);
  return {p,h,argument,canonical,data,source};
}
try {
  for(const anchor of config.anchors) {
    if(!Number.isSafeInteger(anchor.rva)||!/^[0-9a-f]{32}$/.test(anchor.prefix_hex))throw new Error('Invalid code anchor');
    const address=base.add(anchor.rva);if(!inImage(address,16))throw new Error('Anchor outside image');page(address,16,true);
    const actual=[...new Uint8Array(address.readByteArray(16))].map(n=>n.toString(16).padStart(2,'0')).join('');
    if(actual!==anchor.prefix_hex)throw new Error('Loaded native code anchor differs');
  }
  listener=Interceptor.attach(base.add(0xc0f7a0),{
    onEnter(args) {
      if(stopped||pending)return;
      // The same base state-machine method is also used by the perf checker.
      // Only the observed WorldChrMan->creator callsite may run this experiment.
      if(!this.returnAddress.equals(base.add(0xa50a31)))return;
      ticks++;
      if(ticks>240){schedule('frame_budget');return;}
      try {
        const m=q(base.add(0x3d7a1e0));
        if(stage!=='armed'&&stage!=='calling') {
          if(this.threadId!==nativeThread||!m.equals(world)||!q(world.add(0x88)).equals(primary)||
              !q(originalHeader.array.add(0x38)).equals(actor))throw new Error('Live actor/world links changed');
        }
        if(stage==='armed') {
          const checked=before(m,args[0]);world=m;primary=checked.p;originalHeader=checked.h;nativeThread=this.threadId;
          const position=pose(primary);getForward(primary,forwardBuffer);const forward=floats(forwardBuffer);
          const length=Math.hypot(forward[0],forward[1],forward[2]);
          if(!validVector(forward)||Math.abs(forward[3])>0.001||length<0.5||length>1.5)throw new Error('Native forward vector unavailable');
          targetPosition=position.map((v,i)=>i<3?v+forward[i]/length*2.2:v);
          targetPosition.forEach((v,i)=>placement.add(i*4).writeFloat(v));
          // Mirrors the native 7f8730 fields, using live corroborated sources.
          // Kind0 selects the concrete base ChrManipulator: no local pad input.
          params.writeU32(checked.canonical);params.add(4).writeU32(0);params.add(8).writePointer(checked.source);
          params.add(16).writePointer(ptr(0));params.add(24).writePointer(checked.data);params.add(32).writeU32(0);
          if(!q(world.add(0x88)).equals(primary))throw new Error('Primary changed before creation');
          stage='calling';calls=1;
          send({type:'spawn_call_enter',pid:Process.id,thread_id:nativeThread,additional_slot:1,
            argument_value:checked.argument,canonical_config_value:checked.canonical,manipulator_kind:0});
          const returned=create(world,checked.argument,params);
          const candidate=q(originalHeader.array.add(0x38));
          if(candidate.isNull())throw new Error('Native factory produced no additional registry entry');
          requireType(candidate,'.?AVPlayerIns@NS_SPRJ@@',0x2a2b338);
          if(!q(world.add(0x88)).equals(primary)||!q(originalHeader.array).equals(primary))throw new Error('Primary changed after native creation');
          actor=candidate;const actualManipulator=type(q(actor.add(0x58)));
          if(actualManipulator.name!=='.?AVChrManipulator@NS_SPRJ@@')throw new Error('Unexpected additional-player manipulator');
          createdAt=ticks;stage='placing';
          send({type:'spawn_created',pid:Process.id,actor_type:'.?AVPlayerIns@NS_SPRJ@@',additional_slot:1,
            return_matches_registry:returned.equals(actor),primary_unchanged:true,manipulator:actualManipulator.name,
            primary_position:position.slice(0,3),requested_position:targetPosition.slice(0,3)});
        } else if(stage==='placing') {
          requireType(actor,'.?AVPlayerIns@NS_SPRJ@@',0x2a2b338);
          if(!modelReady(actor)) {
            if(ticks-createdAt>=120)throw new Error('Additional player model/component did not become ready');
            return;
          }
          setPosition(actor,placement);createdAt=ticks;stage='waiting';
          send({type:'spawn_positioned',pid:Process.id,position:pose(actor).slice(0,3),primary_unchanged:true});
        } else if(stage==='waiting' && ticks-createdAt>=60) {
          if(this.threadId!==nativeThread||!m.equals(world))throw new Error('Update thread/world changed');
          const h=header(world);if(!q(world.add(0x88)).equals(primary)||!q(h.array).equals(primary)||!q(h.array.add(0x38)).equals(actor))
            throw new Error('Actor/primary registry changed during settle');
          requireType(actor,'.?AVPlayerIns@NS_SPRJ@@',0x2a2b338);requireType(primary,'.?AVPlayerIns@NS_SPRJ@@',0x2a2b338);
          const actual=pose(actor),p=pose(primary),distance=Math.hypot(...actual.slice(0,3).map((v,i)=>v-p[i]));
          result={additional_player_type:'.?AVPlayerIns@NS_SPRJ@@',additional_slot:1,primary_unchanged:true,
            position:actual.slice(0,3),distance_from_primary:distance,creation_calls:calls,
            visibility_requires_owner_confirmation:true};
          stage='created_verified_registry';send({type:'spawn_verified',pid:Process.id,...result});schedule('completed');
        }
      } catch(error) { errors++;result={error:String(error.message??error),stage,native_creation_calls:calls};schedule('experiment_error'); }
    }
  });
  send({type:'trace_ready',pid:Process.id,action:'spawn_one',native_calls_armed:1,scope:'scratch-profile local PlayerIns only'});
  setTimeout(()=>finish('duration'),10000);
} catch(error) {
  send({type:'setup_error',pid:Process.id,message:String(error.message??error)});finish('setup_error');
}
