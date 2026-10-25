'use strict';
const cfg=__CONFIG__;
if(Process.id!==cfg.pid||Process.arch!=='x64'||!Process.mainModule.path.includes('Sekiro-SkCoop-Test'))throw Error('Selected protected target differs');
const b=Process.mainModule.base,q=p=>p.readPointer();
function bytes(p,n){return Array.from(new Uint8Array(p.readByteArray(n))).map(x=>x.toString(16).padStart(2,'0')).join('');}
for(const a of cfg.anchors)if(bytes(b.add(a.rva),16)!==a.hex)throw Error('Native entry anchor differs');
const world=q(b.add(0x3d7a1e0)),primary=q(world.add(0x88)),array=q(world.add(0x48)),guest=q(array.add(0x38)),creator=q(world.add(0x3148));
if(!q(world).equals(b.add(0x2a30058))||!q(primary).equals(b.add(0x2a2b338))||!q(guest).equals(b.add(0x2a2b338))||!q(array).equals(primary))throw Error('Selected actor registry differs');
const root=q(guest.add(0x1ff8)),behavior=q(root.add(0x28)),primaryBehavior=q(q(primary.add(0x1ff8)).add(0x28)),graph=q(behavior.add(0x10)),primaryGraph=q(primaryBehavior.add(0x10)),physics=q(root.add(0x68));
if(!q(behavior).equals(b.add(0x2a790a8))||!q(behavior.add(8)).equals(guest)||graph.isNull()||graph.equals(primaryGraph)||!q(physics.add(8)).equals(guest))throw Error('Selected behavior owner differs');
const speedIndex=behavior.add(0x988).readS32(),angleIndex=behavior.add(0x984).readS32();
if(speedIndex!==213||angleIndex!==209||behavior.add(0xaf0).readU64().toNumber()!==91)throw Error('Native variable binding differs');
const options={abi:'win64',scheduling:'cooperative',exceptions:'propagate'};
const handle=new NativeFunction(b.add(0xffe740),'pointer',['pointer','pointer','int'],options);
const getFloat=new NativeFunction(b.add(0xff3250),'float',['pointer'],options);
const setFloat=new NativeFunction(b.add(0xff3570),'void',['pointer','float'],options);
const user32=Process.getModuleByName('user32.dll');
const key=new NativeFunction(user32.getExportByName('GetAsyncKeyState'),'int16',['int'],options);
const foreground=new NativeFunction(user32.getExportByName('GetForegroundWindow'),'pointer',[],options);
const windowPid=new NativeFunction(user32.getExportByName('GetWindowThreadProcessId'),'uint',['pointer','pointer'],options);
const ownPid=Memory.alloc(4),speedHandle=Memory.alloc(24),angleHandle=Memory.alloc(24);
let listener,stopped=false,initialized=false,armed=false,started=0,reads=0,writes=0,errors=0,ticks=0,thread=0,nativeCalls=0;
let beforeSpeed=0,beforeAngle=0,origin=null,maximumDistance=0,samples=[],released=false,pendingReason=null;
function vec(p){return [0,4,8,12].map(o=>p.add(o).readFloat());}
function links(){if(!q(b.add(0x3d7a1e0)).equals(world)||!q(world.add(0x88)).equals(primary)||!q(array).equals(primary)||!q(array.add(0x38)).equals(guest)||!q(guest.add(0x1ff8)).equals(root)||!q(root.add(0x28)).equals(behavior)||!q(behavior.add(8)).equals(guest)||!q(behavior.add(0x10)).equals(graph))throw Error('Selected incarnation changed');}
function release(){if(!armed||released)return;links();setFloat(speedHandle,beforeSpeed);setFloat(angleHandle,beforeAngle);nativeCalls+=2;released=true;}
function finish(reason,inContext=false){if(stopped)return;if(armed&&!released&&!inContext){pendingReason=reason;send({type:'stop_pending_next_owned_tick',pid:Process.id});return;}let releaseError=null;try{release();}catch(e){releaseError=String(e);}stopped=true;if(listener)listener.detach();send({type:'trace_end',pid:Process.id,reason,callback_errors:errors,listeners_detached:true,inputs_released:!armed||released,release_error:releaseError,native_calls:nativeCalls,native_guest_variable_writes:writes,primary_writes:0,ticks,reads,maximum_distance:maximumDistance,samples});}
listener=Interceptor.attach(b.add(0xc0f7a0),{onEnter(args){if(stopped||!args[0].equals(creator))return;try{
 links();if(!thread)thread=this.threadId;if(thread!==this.threadId)throw Error('Creator thread changed');ticks++;if(pendingReason){finish(pendingReason,true);return;}
 if(!initialized){handle(graph,speedHandle,speedIndex);handle(graph,angleHandle,angleIndex);nativeCalls+=2;beforeSpeed=getFloat(speedHandle);beforeAngle=getFloat(angleHandle);nativeCalls+=2;if(!Number.isFinite(beforeSpeed)||Math.abs(beforeSpeed)>2||!Number.isFinite(beforeAngle)||Math.abs(beforeAngle)>360)throw Error('Unexpected scalar values');initialized=true;send({type:'native_ready',pid:Process.id,before_speed:beforeSpeed,before_angle:beforeAngle,arm_key:'F7',duration_ms:8000,distance_budget:3});}
 if(!armed){ownPid.writeU32(0);windowPid(foreground(),ownPid);if(ownPid.readU32()!==Process.id||!(key(0x76)&0x8000))return;armed=true;started=Date.now();origin=vec(physics.add(0x80));send({type:'native_started',pid:Process.id});}
 const p=vec(physics.add(0x80)),distance=Math.hypot(...p.slice(0,3).map((x,i)=>x-origin[i]));if(!Number.isFinite(distance))throw Error('Non-finite pose');maximumDistance=Math.max(maximumDistance,distance);
 if(distance>=3){finish('distance_budget',true);return;}if(Date.now()-started>=8000){finish('completed',true);return;}if(key(0x79)&0x8000){finish('f10',true);return;}
 setFloat(speedHandle,.75);setFloat(angleHandle,0);nativeCalls+=2;writes+=2;
 if(ticks%12===0){const speed=getFloat(speedHandle),angle=getFloat(angleHandle);nativeCalls+=2;reads+=2;if(samples.length<60)samples.push({elapsed_ms:Date.now()-started,speed,angle,distance});if(Math.abs(speed-.75)>.01)throw Error('Native scalar write was not applied');}
 }catch(e){errors++;send({type:'probe_error',message:String(e),stack:e.stack});finish('error',true);}}});
send({type:'trace_ready',pid:Process.id,scope:'F7 arms bounded native MoveSpeedLevel/MoveAngle commands on additional actor only',duration_ms:180000});
setTimeout(()=>finish('arm_deadline'),180000);
