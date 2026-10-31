#include <Windows.h>
#include "skcoop/lan/peer.hpp"
#include "skcoop/actors/native_input_latch.hpp"
#include "skcoop/actors/native_command_mailbox.hpp"
#include "launch_settings.h"
#include "lan_payload.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <cmath>
using namespace skcoop;
namespace {
void require(bool v){if(!v)throw std::runtime_error("Native command transport invariant");}
lan::Build build(){lan::Build b;b.purpose=lan::Purpose::native_commands;b.adapter=lan::command_adapter;b.game[0]=1;b.scenario[0]=2;return b;}
lan::Ticket ticket(){return {"127.0.0.1",0,lan::random_id(),lan::random_key()};}
void deliver(lan::Session& a,lan::Session& b,std::uint64_t now){for(const auto& p:a.take_outgoing())require(b.receive(p,now));}
void codecs_and_scope(){
 auto t=ticket();lan::Session a({lan::Role::host,t,build(),11}),b({lan::Role::guest,t,build(),22});
 a.opened(1,lan::random_id());b.opened(1,{});deliver(a,b,2);deliver(b,a,3);deliver(a,b,4);deliver(b,a,5);deliver(a,b,6);
 actors::PlayerCommandCapture source(7,3);require(source.capture({},1));
 a.send_command(*source.latest(),true);auto p=a.take_outgoing().front();
 const auto bytes=lan::encode(p,t.key);require(bytes.size()==lan::header_bytes+lan::tag_bytes+lan::command_body_bytes);
 const auto decoded=lan::decode(bytes,t.room,t.key);require(decoded&&decoded.packet->command==*source.latest()&&decoded.packet->command_enabled);
 require(b.receive(*decoded.packet,7));const auto received=b.take_remote_command();require(received.has_value());
 require(received->scope.session==t.room&&received->scope.connection==p.connection&&received->scope.player==11&&received->scope.input_generation==7);
 require(!b.receive(p,8)&&!b.take_remote_command());
 p.sequence++;p.connection=lan::random_id();require(!b.receive(p,9)&&!b.take_remote_command());
 auto damaged=bytes;damaged[100]^=1;require(!lan::decode(damaged,t.room,t.key));
 damaged=bytes;damaged[4]=3;require(!lan::decode(damaged,t.room,t.key));
 for(std::size_t n=0;n<bytes.size();++n)require(!lan::decode(std::span(bytes).first(n),t.room,t.key));
 auto unsupported=*source.latest();unsupported.input.held=actors::button_mask(actors::CommandButton::jump);p.command=unsupported;p.sequence++;
 bool refused=false;try{(void)lan::encode(p,t.key);}catch(...){refused=true;}require(refused);
 // Legacy/checker peers reject commands even when their envelopes authenticate.
 lan::Session old({lan::Role::host,t,{},31}),legacy({lan::Role::guest,t,{},32});old.opened(1,lan::random_id());legacy.opened(1,{});
 deliver(old,legacy,2);deliver(legacy,old,3);deliver(old,legacy,4);deliver(legacy,old,5);deliver(old,legacy,6);
 old.heartbeat();p=old.take_outgoing().front();p.kind=lan::Kind::command;p.command=*source.latest();require(!legacy.receive(p,10)&&legacy.failure()==lan::Failure::protocol);
 const std::string ini="[LAUNCH]\ntest_executable=C:\\Test\\sekiro.exe\ncoop_profile_root=C:\\Profile\naccount=76561198000000001\nreceipt_directory=C:\\Receipts\nactor_test=lan-native-commands\nlan_role=host\nlan_code="+lan::ticket_text({"127.0.0.1",24860,t.room,t.key})+"\n";
 auto settings=launcher::parse_launch_settings(ini);require(settings.actor_native_lan_test&&settings.actor_lan_test&&settings.independent_profile&&!settings.actor_native_commands_test);
 refused=false;try{(void)launcher::parse_launch_settings(ini+"lan_profile=combat-preview\n");}catch(...){refused=true;}require(refused);
 launch::Payload payload{};payload.actor_test=launch::ActorTest::lan_native_commands;payload.lan_role=1;payload.lan_profile=1;
 const auto code=lan::ticket_text({"127.0.0.1",24860,t.room,t.key});std::copy(code.begin(),code.end(),payload.lan_ticket);
 std::copy(lan::independent_motion_scenario.begin(),lan::independent_motion_scenario.end(),payload.scenario_sha256);require(actors::valid_lan_payload(payload));
 payload.lan_profile=2;require(!actors::valid_lan_payload(payload));
}
std::uint64_t ticks{};std::uint64_t clock_read()noexcept{return ticks;}
void continuous_observation(){
 actors::NativeCommandMailbox box(1,&clock_read);require(box.configure_movement_onset(actors::NativeMovementOnset::observe_continuous));
 actors::CommandDelivery d;d.baseline=true;d.state={1,1,1,{{{0,1},.35f},0}};
 require(box.offer(d)==actors::NativeInputOffer::accepted);const auto frame=box.begin_update();require(frame.accepted&&box.finish_update(frame.epoch));
 require(!box.configure_movement_onset(actors::NativeMovementOnset::strict));
 require(!box.observe_resolver(frame.epoch,{{1,0},.35f},true,true,true)&&!box.failed()&&!box.consumed());
 require(box.observe_held(frame.epoch,actors::CommandButton::attack,0)&&box.observe_held(frame.epoch,actors::CommandButton::guard,0));
 require(!box.consumed());require(box.observe_resolver(frame.epoch,frame.input.movement,true,true,true));require(box.consumed().has_value());
 require(box.observe_held(frame.epoch,actors::CommandButton::guard,1)==false&&box.failed());
}
int child(bool host,const std::filesystem::path& room_file,const std::filesystem::path& result_file){
 try{
  auto t=ticket();
  if(!host){for(unsigned n=0;n<1000&&!std::filesystem::exists(room_file);++n)Sleep(10);
   std::ifstream f(room_file);std::string code;std::getline(f,code);const auto value=lan::parse_ticket(code);require(value.has_value());t=*value;}
  lan::Peer peer({host?lan::Role::host:lan::Role::guest,t,build(),host?101u:202u},"127.0.0.1");
  if(host){for(unsigned n=0;n<1000&&peer.read().status.state!=lan::State::listening;++n)Sleep(10);
   t.port=peer.read().status.local_port;require(t.port!=0);const auto temp=room_file.wstring()+L".tmp";
   {std::ofstream f(temp);f<<lan::ticket_text(t);}std::filesystem::rename(temp,room_file);}
  const auto started=GetTickCount64();std::uint64_t active{},offered{},last_sequence{};unsigned attack_edges{},guard_edges{},guard_releases{};
  actors::PlayerCommandCapture capture(1,3);actors::PlayerCommandReceiver receiver(3);actors::NativeInputLatch latch(1);actors::ButtonMask previous{};
  std::array<lan::ReceivedCommand,actors::command_queue_capacity> incoming;bool complete=false;
  while(GetTickCount64()-started<10000){const auto now=GetTickCount64();const auto view=peer.read();
   if(view.status.state==lan::State::failed||view.status.state==lan::State::lost){require(complete);break;}
   if(view.status.state==lan::State::connected){
    if(!active)active=now;const auto elapsed=now-active;
    const bool following=!(elapsed>=1250&&elapsed<1450);
    if(!following){receiver.suspend();latch.suspend();}
    peer.try_set_command_following(following);
    actors::PlayerInput input;
    if((elapsed>=300&&elapsed<350)||(elapsed>=1250&&elapsed<1600)||(elapsed>=1900&&elapsed<1950))input.held=1;
    if(elapsed>=600&&elapsed<1100)input.held=2;
    require(capture.capture(input,elapsed));
    if(now-offered>=20&&peer.try_offer_command(*capture.latest(),!(elapsed>=1250&&elapsed<1450))){offered=now;require(capture.offered(capture.latest()->transition_latest));}
    lan::Motion motion;motion.epoch=1;motion.descriptor=0xa000;motion.position[0]=host?1.f:2.f;peer.try_offer(motion);
    std::size_t count{};peer.try_take_commands(incoming,count);
    for(std::size_t n=0;n<count;++n){const auto& m=incoming[n];require(m.scope.player==(host?202u:101u)&&m.packet_sequence>last_sequence);last_sequence=m.packet_sequence;
     require(receiver.observe(m.scope,m.command,m.arrival_ms,m.enabled)!=actors::CommandReceive::failed);}
    if(const auto d=receiver.take(now)){if(d->neutralize)latch.suspend();else require(latch.offer(*d,now)==actors::NativeInputOffer::accepted);}
    const auto actual=latch.begin_update(now).held;
    if((actual&1)&&!(previous&1))++attack_edges;
    if((actual&2)&&!(previous&2))++guard_edges;
    if(!(actual&2)&&(previous&2))++guard_releases;previous=actual;
    if(elapsed>2300){require(attack_edges==2&&guard_edges==1&&guard_releases==1&&actual==0&&view.status.frames_received>0);complete=true;}
    if(elapsed>2600)break;
   }
   Sleep(5);
  }
  require(complete);peer.stop();std::ofstream(result_file)<<"PASS 2 attacks 1 held guard 1 release; paused held attack suppressed\n";return 0;
 }catch(const std::exception& e){std::ofstream(result_file)<<"FAIL "<<e.what();return 1;}
}
void processes(const wchar_t* executable){
 const auto folder=std::filesystem::temp_directory_path()/(L"skcoop-native-command-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 require(std::filesystem::create_directory(folder));const auto room=folder/L"room.txt";
 const auto spawn=[&](const wchar_t* role,const std::filesystem::path& output){
  std::wstring cmd=L"\""+std::wstring(executable)+L"\" --child "+role+L" \""+room.wstring()+L"\" \""+output.wstring()+L"\"";
  STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION process{};require(CreateProcessW(executable,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&process));CloseHandle(process.hThread);return process.hProcess;};
 const HANDLE children[]{spawn(L"host",folder/L"host.txt"),spawn(L"guest",folder/L"guest.txt")};
 const auto wait=WaitForMultipleObjects(2,children,TRUE,20000);
 bool good=wait==WAIT_OBJECT_0;for(const auto process:children){DWORD code{};if(wait!=WAIT_OBJECT_0)TerminateProcess(process,9);require(GetExitCodeProcess(process,&code));good=good&&code==0;CloseHandle(process);}
 for(const auto name:{L"host.txt",L"guest.txt"}){std::ifstream f(folder/name);std::string line;std::getline(f,line);std::cout<<line<<'\n';good=good&&line.starts_with("PASS");}
 std::filesystem::remove_all(folder);require(good);
}
}
int wmain(int argc,wchar_t** argv){try{
 if(argc==5&&std::wstring_view(argv[1])==L"--child")return child(std::wstring_view(argv[2])==L"host",argv[3],argv[4]);
 codecs_and_scope();continuous_observation();processes(argv[0]);
 std::cout<<"PASS authenticated native commands in two distinct processes, press/hold/release/pause and strict consumption separation; no game run\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
