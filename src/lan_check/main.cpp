#include <Windows.h>
#include <shellapi.h>
#include "skcoop/lan/peer.hpp"
#include "skcoop/lan/discovery.hpp"
#include "protected_launch.h"
#include "file_open_error.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
using namespace skcoop::lan;
namespace fs=std::filesystem;
namespace {
HWND window{},rooms_box{},status_box{},create_button{},join_button{};
std::unique_ptr<Discovery> browser,advertiser;std::vector<Room> rooms;
std::thread job;std::atomic<bool> done{};bool busy{};unsigned role{};Ticket active_ticket;
std::mutex state_mutex;skcoop::launcher::LaunchStatus launch_state;std::wstring job_error;
std::wstring wide(std::string_view s){return skcoop::launcher::diagnostic_wide(s);}
fs::path directory(){std::wstring s(32768,0);const auto n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));if(!n||n==s.size())throw std::runtime_error("Pasta do programa indisponivel");s.resize(n);return fs::path(s).parent_path();}
void report(const fs::path& path,const Status& s,bool success){
 std::ostringstream out;out<<"{\"kind\":\"lan-connection-check\",\"wire_protocol\":1,\"native_game_test\":false,\"passed\":"<<(success?"true":"false")<<",\"state\":"<<static_cast<int>(s.state)<<",\"error\":"<<static_cast<int>(s.failure)<<",\"frames_sent\":"<<s.frames_sent<<",\"frames_received\":"<<s.frames_received<<",\"rejected\":"<<s.rejected<<"}\n";
 const auto value=out.str();HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create new report");DWORD n{};const bool ok=WriteFile(h,value.data(),static_cast<DWORD>(value.size()),&n,nullptr)&&n==value.size()&&FlushFileBuffers(h);CloseHandle(h);if(!ok)throw std::runtime_error("Cannot finish report");
}
int headless(const fs::path& path,const fs::path& output){
 std::ifstream f(path,std::ios::binary);std::string r,code,extra;std::getline(f,r);std::getline(f,code);if(!f||std::getline(f,extra)||code.size()>160||(r!="host"&&r!="guest"))return 2;
 const auto t=parse_ticket(code);if(!t)return 2;Peer p({r=="host"?Role::host:Role::guest,*t,{},random_participant()});const auto start=GetTickCount64();bool success=false;Status s,confirmed;
 while(GetTickCount64()-start<12000){Motion m;m.epoch=1;p.try_offer(m);s=p.read().status;
  if(s.state==State::connected&&s.frames_sent>=20&&s.frames_received>=20){success=true;confirmed=s;if(GetTickCount64()-start>=4000)break;}
  if(s.state==State::failed||s.state==State::lost)break;Sleep(20);
 }p.stop();report(output,success?confirmed:s,success);return success?0:1;
}
void controls(){EnableWindow(create_button,!busy);EnableWindow(join_button,!busy&&!rooms.empty());EnableWindow(rooms_box,!busy);}
void start(unsigned selected_role){
 if(busy)return;Ticket ticket;
 if(selected_role==1)ticket={"127.0.0.1",24860,random_id(),random_key()};
 else{const auto selected=SendMessageW(rooms_box,LB_GETCURSEL,0,0);if(selected==LB_ERR||static_cast<std::size_t>(selected)>=rooms.size())throw std::runtime_error("Selecione o PC encontrado na lista.");ticket=rooms[static_cast<std::size_t>(selected)].ticket;}
 const auto internal=directory()/L"SeamlessCoop";
 advertiser.reset();browser.reset();rooms.clear();SendMessageW(rooms_box,LB_RESETCONTENT,0,0);busy=true;done=false;role=selected_role;active_ticket=ticket;
 {std::lock_guard lock(state_mutex);launch_state={};job_error.clear();}controls();SetWindowTextW(status_box,L"Preparando o perfil desta conta e conferindo a copia do jogo...\r\nA primeira preparacao pode copiar cerca de 25 GB. Aguarde.");
 try{job=std::thread([internal,ticket,selected_role]{try{
  const auto config=skcoop::launcher::prepare_direct_lan(internal,selected_role,ticket_text(ticket));
  const auto result=skcoop::launcher::direct_protected_launch(config,internal,[](const auto& update){std::lock_guard lock(state_mutex);launch_state=update;});
  if(result){std::lock_guard lock(state_mutex);job_error=L"O jogo nao terminou normalmente. Os registros estao na pasta SeamlessCoop/receipts.";}
 }catch(const std::exception& e){std::lock_guard lock(state_mutex);job_error=wide(e.what());}catch(...){std::lock_guard lock(state_mutex);job_error=L"Nao foi possivel concluir esta abertura.";}done=true;});}
 catch(...){busy=false;controls();throw;}
}
bool same_rooms(const std::vector<Room>& a,const std::vector<Room>& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(a[i].ticket!=b[i].ticket)return false;return true;}
void update(){
 if(!busy){if(!browser)browser=std::make_unique<Discovery>();const auto found=browser->read();
  if(!same_rooms(rooms,found.rooms)){rooms=found.rooms;SendMessageW(rooms_box,LB_RESETCONTENT,0,0);for(const auto& room:rooms){const auto label=L"PC encontrado - "+wide(room.ticket.address);SendMessageW(rooms_box,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}if(!rooms.empty())SendMessageW(rooms_box,LB_SETCURSEL,0,0);controls();}
  if(found.error)SetWindowTextW(status_box,L"A busca de salas encontrou um problema de rede. Confira a permissao do Firewall na rede privada.");return;
 }
 if(done){job.join();busy=false;advertiser.reset();std::wstring error;skcoop::launcher::LaunchStatus state;{std::lock_guard lock(state_mutex);error=job_error;state=launch_state;}controls();
  if(!error.empty()){SetWindowTextW(status_box,L"A abertura foi interrompida. Seu perfil de teste foi mantido.");MessageBoxW(window,error.c_str(),L"Sekiro Co-op",MB_OK|MB_ICONERROR);}
  else SetWindowTextW(status_box,state.completed?L"Jogo fechado normalmente. O progresso deste teste foi preservado.\r\nPara jogar novamente, use Criar jogo ou Entrar.":L"A abertura terminou antes de completar o teste.");return;
 }
 skcoop::launcher::LaunchStatus state;{std::lock_guard lock(state_mutex);state=launch_state;}
 if(!state.ready)return;
 if(role==1&&!advertiser&&state.network==static_cast<unsigned>(State::listening)&&(state.hooks&3)==3)advertiser=std::make_unique<Discovery>(active_ticket);
 if(advertiser&&state.network!=static_cast<unsigned>(State::listening))advertiser.reset();
 std::wstring label=L"Sekiro aberto com o perfil de teste desta conta.\r\nNa primeira vez escolha Novo Jogo; nas proximas, Continuar.";
 if(state.network_error==4)label+=L"\r\nVersoes incompativeis. Atualize o pacote nos dois computadores.";
 else if(state.actor_error||state.network_error)label+=L"\r\nO teste de movimento foi interrompido. Feche o jogo pelo menu para repetir.";
 else if(state.actor_phase==5||state.network>=static_cast<unsigned>(State::lost))label+=L"\r\nO teste terminou ou o parceiro saiu. Feche pelo menu e abra novamente para repetir.";
 else if(state.network==static_cast<unsigned>(State::listening))label+=L"\r\nAguardando o notebook. Nele, selecione este PC e clique em Entrar.";
 else if(state.network==static_cast<unsigned>(State::connected)){
  if(state.waiting==3)label+=L"\r\nAnde alguns passos em um computador para separar os dois jogadores.";
  else if(state.waiting==4)label+=L"\r\nFiquem no mesmo local seguro e perto um do outro.";
  else if(state.actor_phase==4)label+=state.paused?L"\r\nRepresentacao remota pausada. F9 retoma.":L"\r\nMovimento remoto ativo.";
  else if(state.actor_phase==2||state.actor_phase==3)label+=L"\r\nPreparando o personagem do parceiro...";
  else label+=L"\r\nConectados. No mesmo local seguro, pressione F8 nos dois jogos.";
 }else label+=L"\r\nCarregando a conexao...";
 if(advertiser&&advertiser->read().error)label+=L"\r\nNao foi possivel anunciar a sala. Confira se outro lancador esta aberto.";
 std::wostringstream text;text<<label<<L"\r\nMovimentos enviados: "<<state.sent<<L"    recebidos: "<<state.received;SetWindowTextW(status_box,text.str().c_str());
}
HWND control(const wchar_t* kind,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id=0){auto c=CreateWindowExW(0,kind,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return c;}
LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM w,LPARAM l){try{switch(message){
 case WM_CREATE:window=h;
  control(L"STATIC",L"Sekiro Co-op - teste de movimento na rede local",0,20,18,640,26);
  control(L"STATIC",L"Steam aberta nos dois computadores, cada um na sua conta.\r\nNo PC, clique em Criar jogo. No notebook, selecione o PC encontrado e clique em Entrar.",0,20,55,640,52);
  create_button=control(L"BUTTON",L"Criar jogo neste PC",WS_TABSTOP,20,120,230,36,1);
  control(L"STATIC",L"PCs disponiveis na sua rede:",0,20,178,640,24);
  rooms_box=control(L"LISTBOX",L"",WS_BORDER|LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,20,207,640,100,3);
  join_button=control(L"BUTTON",L"Entrar no PC selecionado",WS_TABSTOP,20,322,250,36,2);EnableWindow(join_button,FALSE);
  status_box=control(L"STATIC",L"Procurando salas automaticamente...\r\nNao e preciso copiar codigos nem alterar as propriedades da Steam.",0,20,383,640,128);
  control(L"STATIC",L"Perfil de teste novo por conta; saves pessoais nao sao importados.\r\nPrimeira vez: Novo Jogo nos dois, pule as cenas e fique no mesmo local seguro.\r\nAnde alguns passos em um computador e pressione F8 nos dois jogos.\r\nF9: pausar. F10: encerrar o movimento remoto.\r\nPrototipo limitado: sem ataques, dano, inimigos ou campanha sincronizada.",0,20,527,640,78);
  SetTimer(h,1,200,nullptr);return 0;
 case WM_TIMER:update();return 0;
 case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){if(LOWORD(w)==1)start(1);else if(LOWORD(w)==2)start(2);}return 0;
 case WM_CLOSE:if(busy&&done)update();if(busy){MessageBoxW(h,L"Feche o Sekiro pelo menu antes de fechar o lancador. Durante a copia inicial, aguarde a preparacao terminar.",L"Teste em andamento",MB_OK);return 0;}browser.reset();advertiser.reset();DestroyWindow(h);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 }}catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Sekiro Co-op",MB_OK|MB_ICONERROR);}return DefWindowProcW(h,message,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){try{
 int count{};auto** args=CommandLineToArgvW(GetCommandLineW(),&count);if(count==4&&std::wstring_view(args[1])==L"--check"){const fs::path input=args[2],output=args[3];LocalFree(args);return headless(input,output);}LocalFree(args);
 WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"SkCoopSimpleLauncher";if(!RegisterClassW(&cls))return 2;
 const auto h=CreateWindowExW(0,cls.lpszClassName,L"Sekiro Co-op",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,710,660,nullptr,nullptr,instance,nullptr);if(!h)return 2;ShowWindow(h,show);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(h,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}return static_cast<int>(m.wParam);
 }catch(...){return 2;}}
