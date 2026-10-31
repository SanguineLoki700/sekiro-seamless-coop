#include <Windows.h>
#include <shellapi.h>
#include "skcoop/lan/peer.hpp"
#include "skcoop/lan/discovery.hpp"
#include "skcoop/lan/local_preview.hpp"
#include "skcoop/actors/controller_scenario.hpp"
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
HWND window{},rooms_box{},status_box{},create_button{},join_button{},preview_button{},native_button{},instructions_box{};
std::unique_ptr<Discovery> browser,advertiser;std::vector<Room> rooms;
std::thread job;std::atomic<bool> done{};bool busy{};unsigned role{};Ticket active_ticket;
std::mutex state_mutex;skcoop::launcher::LaunchStatus launch_state;std::wstring job_error;
LocalPreviewStatus preview_state;
bool preview_receipt_failed{};
// The launcher's directory chain remains pinned while this observer first
// opens the companion. Subsequent writes use that same exclusive new handle,
// including the final write after the launcher has released its directory pins.
class PreviewReceipt {
 fs::path folder_;HANDLE handle_{INVALID_HANDLE_VALUE};std::string native_receipt_;
 bool failed_{},written_{};std::uint64_t last_at_{},records_{};
 LocalPreviewPhase previous_{LocalPreviewPhase::starting};
 static bool valid_name(std::string_view name){
  constexpr std::string_view prefix="game-test-",suffix=".jsonl";
  if(name.size()!=prefix.size()+32+suffix.size()||!name.starts_with(prefix)||!name.ends_with(suffix))return false;
  for(const auto c:name.substr(prefix.size(),32))if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
  return true;
 }
public:
 explicit PreviewReceipt(fs::path folder):folder_(std::move(folder)){}
 ~PreviewReceipt(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
 bool failed()const noexcept{return failed_;}
 void observe(const skcoop::launcher::LaunchStatus& game,const LocalPreviewStatus& local,bool final=false)noexcept{
  if(failed_)return;
  try{
   if(game.receipt_name.empty())return;
   if(!valid_name(game.receipt_name)){failed_=true;return;}
   if(handle_==INVALID_HANDLE_VALUE){
    native_receipt_=game.receipt_name;
    const auto path=folder_/fs::path(native_receipt_+".preview.jsonl");
    handle_=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle_==INVALID_HANDLE_VALUE){failed_=true;return;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(handle_,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){failed_=true;return;}
   }else if(native_receipt_!=game.receipt_name){failed_=true;return;}
   const auto now=GetTickCount64();
   if(!final&&written_&&local.phase==previous_&&now-last_at_<5000)return;
   if(!final&&records_>=256&&!(finished(local.phase)&&(!written_||local.phase!=previous_)))return;
   // Terminal cause and final counters keep reserved records after samples fill.
   std::ostringstream out;out<<"{\"event\":\""<<(final?"final":"snapshot")<<"\",\"native_receipt\":\""<<native_receipt_
    <<"\",\"phase\":\""<<local_preview_phase_name(local.phase)<<"\",\"previous_phase\":\""<<local_preview_phase_name(previous_)
    <<"\",\"end_cause\":\""<<(finished(local.phase)?local_preview_phase_name(local.phase):"none")
    <<"\",\"elapsed_ms\":"<<local.elapsed_ms<<",\"failure\":"<<static_cast<unsigned>(local.failure)
    <<",\"action_fault\":"<<static_cast<unsigned>(local.action_fault)<<",\"frames_sent\":"<<local.sent
    <<",\"frames_received\":"<<local.received<<",\"offers\":"<<local.offers<<",\"mirrored_serial\":"<<local.mirrored_serial
    <<",\"path_pauses\":"<<local.path_pauses<<",\"stale_pauses\":"<<local.stale_pauses
    <<",\"child_pid\":"<<game.pid<<",\"game_finished\":"<<(game.finished?"true":"false")
    <<",\"game_completed\":"<<(game.completed?"true":"false")<<"}\n";
   const auto line=out.str();DWORD count{};
   if(line.size()>2048||!WriteFile(handle_,line.data(),static_cast<DWORD>(line.size()),&count,nullptr)||count!=line.size()||!FlushFileBuffers(handle_)){failed_=true;return;}
   ++records_;written_=true;previous_=local.phase;last_at_=now;
  }catch(...){failed_=true;}
 }
};
std::wstring wide(std::string_view s){return skcoop::launcher::diagnostic_wide(s);}
const wchar_t* native_phase_text(std::uint32_t value){
 using Phase=skcoop::actors::ControllerScenarioPhase;
 switch(static_cast<Phase>(value)){
 case Phase::waiting_ready:return L"Aguardando carregamento e preparacao do personagem";
 case Phase::neutral:return L"Conferindo repouso";
 case Phase::moving:return L"Executando movimento independente";
 case Phase::movement_release:return L"Conferindo a parada";
 case Phase::attack_press:return L"Executando ataque";
 case Phase::attack_release:return L"Conferindo o fim do ataque";
 case Phase::guard_hold:return L"Mantendo a defesa";
 case Phase::guard_release:return L"Soltando a defesa";
 case Phase::final_neutral:return L"Conferindo o repouso final";
 case Phase::complete:return L"Sequencia automatica concluida; resultado provisorio";
 case Phase::stopped:return L"Sequencia automatica encerrada";
 case Phase::failed:return L"Verificacao interrompida";
 case Phase::inconclusive:return L"Coleta automatica concluida; resultado das acoes inconclusivo";
 }
 return L"Preparando a verificacao";
}
std::wstring native_status(const skcoop::launcher::LaunchStatus& state,bool closed=false){
 using Phase=skcoop::actors::ControllerScenarioPhase;const auto phase=static_cast<Phase>(state.native_controller_phase);
 std::wostringstream label;label<<L"Teste automatico de comandos independentes neste PC.\r\n"
  <<(phase==Phase::complete&&!state.native_controller_output_bound?L"Sequencia encerrada sem validar o resultado das acoes":native_phase_text(state.native_controller_phase))<<L".";
 if(!state.native_controller_output_bound||phase==Phase::inconclusive)label<<L"\r\nLeitura do resultado das ações ainda em validação.";
 if(phase==Phase::inconclusive&&state.native_controller_fault==static_cast<std::uint32_t>(skcoop::actors::ControllerScenarioFault::action_result_unbound))
  label<<L"\r\nColeta inconclusiva, codigo "<<state.native_controller_fault<<L". Os registros nao atestam aprovacao do controlador.";
 else if(state.native_controller_fault)
  label<<L"\r\nFalha de verificacao: "<<state.native_controller_fault<<L". Confira os registros desta abertura.";
 else if(state.actor_error||state.action_error)
  label<<L"\r\nPreparacao interrompida, codigo "<<(state.actor_error?state.actor_error:state.action_error)<<L". Confira os registros.";
 else if(phase==Phase::complete&&state.native_controller_output_bound)label<<L"\r\nO resultado ainda depende da revisao dos registros.";
 else if(phase==Phase::waiting_ready&&!closed)label<<L"\r\nUse Continuar ou Novo Jogo; fique parado em um trecho plano e seguro, sem inimigos.";
 else if(!skcoop::actors::terminal(phase)&&!closed)label<<L"\r\nMantenha seu personagem parado; os comandos do segundo sao automaticos.";
 if(!closed&&(skcoop::actors::terminal(phase)||state.native_controller_fault))label<<L"\r\nPode fechar o Sekiro normalmente pelo menu.";
 label<<L"\r\nComandos emitidos: "<<state.native_controller_commands<<L"    consumidos: "<<state.native_controller_consumed
  <<L"\r\nDefesa observada: "<<state.native_controller_guard_ms<<L" ms    deslocamento: "<<state.native_controller_displacement_mm<<L" mm"
  <<L"\r\nCodigo de estado nativo: "<<state.native_controller_state_code;
 if(state.native_controller_manual_events||state.native_controller_primary_writes)
  label<<L"\r\nEventos manuais: "<<state.native_controller_manual_events<<L"    alteracoes no original: "<<state.native_controller_primary_writes;
 return label.str();
}
fs::path directory(){std::wstring s(32768,0);const auto n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));if(!n||n==s.size())throw std::runtime_error("Pasta do programa indisponivel");s.resize(n);return fs::path(s).parent_path();}
void report(const fs::path& path,const Status& s,bool success){
 std::ostringstream out;out<<"{\"kind\":\"lan-connection-check\",\"wire_protocol\":"<<version<<",\"native_game_test\":false,\"passed\":"<<(success?"true":"false")<<",\"state\":"<<static_cast<int>(s.state)<<",\"error\":"<<static_cast<int>(s.failure)<<",\"frames_sent\":"<<s.frames_sent<<",\"frames_received\":"<<s.frames_received<<",\"rejected\":"<<s.rejected<<"}\n";
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
void controls(){EnableWindow(create_button,!busy);EnableWindow(preview_button,!busy);EnableWindow(native_button,!busy);EnableWindow(join_button,!busy&&!rooms.empty());EnableWindow(rooms_box,!busy);}
void start(unsigned selected_role){
 if(busy)return;if(selected_role<1||selected_role>4)throw std::runtime_error("Modo de teste desconhecido.");Ticket ticket;const bool preview=selected_role==3,native=selected_role==4;
 if(selected_role==1||preview)ticket={"127.0.0.1",24860,random_id(),random_key()};
 else if(!native){const auto selected=SendMessageW(rooms_box,LB_GETCURSEL,0,0);if(selected==LB_ERR||static_cast<std::size_t>(selected)>=rooms.size())throw std::runtime_error("Selecione o PC encontrado na lista.");ticket=rooms[static_cast<std::size_t>(selected)].ticket;}
 const auto internal=directory()/L"SeamlessCoop";
 advertiser.reset();browser.reset();rooms.clear();SendMessageW(rooms_box,LB_RESETCONTENT,0,0);busy=true;done=false;role=selected_role;active_ticket=ticket;
 {std::lock_guard lock(state_mutex);launch_state={};preview_state={};preview_receipt_failed=false;job_error.clear();}controls();SetWindowTextW(status_box,L"Preparando o perfil desta conta e conferindo a copia do jogo...\r\nA primeira preparacao pode copiar cerca de 25 GB. Aguarde.");
 SetWindowTextW(instructions_box,native?L"Controlador nativo: carregue e fique parado em um trecho plano e vazio.\r\nA sequencia local comeca automaticamente.\r\nAo terminar, feche pelo menu.":preview?L"Diagnostico legado: F8 cria o parceiro simulado; F9 pausa; F10 encerra.\r\nEste botao nao testa o controlador nativo pela rede.":L"Nos dois jogos: carregue no mesmo trecho plano e sem inimigos.\r\nFique parado alguns segundos; o parceiro aparece automaticamente.\r\nTeste caminhada, ataque e defesa. Ao retornar de uma pausa, solte os botoes.\r\nDano, postura, inimigos e campanha ainda nao sao compartilhados.");
 try{job=std::thread([internal,ticket,selected_role,preview,native]{try{
  const auto config=native?skcoop::launcher::prepare_direct_native(internal):skcoop::launcher::prepare_direct_lan(internal,preview?1u:selected_role,ticket_text(ticket),preview);
  std::unique_ptr<LocalPreview> mirror;
  PreviewReceipt receipt(internal/L"receipts");skcoop::launcher::LaunchStatus last_launch;
  const auto finish=[&]{
   // Joining stays on the launcher job, outside game callbacks and GUI locks.
   if(mirror)mirror->stop();const auto local=mirror?mirror->read():LocalPreviewStatus{};
   if(preview)receipt.observe(last_launch,local,true);
   std::lock_guard lock(state_mutex);preview_state=local;preview_receipt_failed=receipt.failed();
  };
  int result{};
  try{result=skcoop::launcher::direct_protected_launch(config,internal,[&](const auto& update){
   last_launch=update;
   if(preview)receipt.observe(update,mirror?mirror->read():LocalPreviewStatus{});
   if(preview&&!mirror&&update.ready&&update.network==static_cast<unsigned>(State::listening)&&(update.hooks&19)==19)
    mirror=std::make_unique<LocalPreview>(ticket);
   const auto local=mirror?mirror->read():LocalPreviewStatus{};
   std::lock_guard lock(state_mutex);launch_state=update;preview_state=local;preview_receipt_failed=receipt.failed();
  });}catch(...){finish();throw;}
  finish();
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
 if(done){job.join();busy=false;advertiser.reset();std::wstring error;skcoop::launcher::LaunchStatus state;bool receipt_failed{};{std::lock_guard lock(state_mutex);error=job_error;state=launch_state;receipt_failed=preview_receipt_failed;}controls();
  if(!error.empty()){SetWindowTextW(status_box,L"A abertura foi interrompida. Seu perfil de teste foi mantido.");if(receipt_failed)error+=L"\r\nNao foi possivel salvar o registro do parceiro simulado.";MessageBoxW(window,error.c_str(),L"Sekiro Co-op",MB_OK|MB_ICONERROR);}
  else{std::wstring label=state.completed?L"Jogo fechado normalmente. O progresso deste teste foi preservado.\r\nPara jogar novamente, use Criar jogo ou Entrar.":L"A abertura terminou antes de completar o teste.";
   if(role==3)label+=receipt_failed?L"\r\nNao foi possivel salvar o registro do parceiro simulado.":L"\r\nO resultado do parceiro simulado foi salvo junto aos registros desta abertura.";
   if(role==4)label+=L"\r\n"+native_status(state,true);SetWindowTextW(status_box,label.c_str());}return;
 }
 skcoop::launcher::LaunchStatus state;LocalPreviewStatus local;bool receipt_failed{};{std::lock_guard lock(state_mutex);state=launch_state;local=preview_state;receipt_failed=preview_receipt_failed;}
 if(!state.ready)return;
 if(role==4){const auto label=native_status(state);SetWindowTextW(status_box,label.c_str());return;}
 if(role==1&&!advertiser&&state.network==static_cast<unsigned>(State::listening)&&(state.hooks&35)==35)advertiser=std::make_unique<Discovery>(active_ticket);
 if(advertiser&&state.network!=static_cast<unsigned>(State::listening))advertiser.reset();
 std::wstring label=role==3?L"Teste local de ataques e defesa: um jogo com parceiro simulado.\r\nUse Continuar ou, na primeira vez, Novo Jogo.":L"Sekiro aberto com o perfil de teste desta conta.\r\nNa primeira vez escolha Novo Jogo; nas proximas, Continuar.";
 if(state.network_error==4)label+=L"\r\nVersoes incompativeis. Atualize o pacote nos dois computadores.";
 else if(state.actor_error||state.action_error)label+=L"\r\nO teste de movimento foi interrompido. Feche o jogo pelo menu para repetir.";
 else if(role==3&&finished(local.phase)){
  if(local.phase==LocalPreviewPhase::time_limit)label+=L"\r\nO teste local atingiu seu limite de tempo. Feche pelo menu para repetir.";
  else if(local.phase==LocalPreviewPhase::source_ended||local.phase==LocalPreviewPhase::stopped)label+=L"\r\nO teste local terminou. Feche pelo menu para repetir.";
  else label+=L"\r\nO teste local encontrou uma falha. Feche pelo menu para repetir.";
 }
 else if(state.network_error)label+=L"\r\nO teste de movimento foi interrompido. Feche o jogo pelo menu para repetir.";
 else if(state.actor_phase==5||state.network>=static_cast<unsigned>(State::lost))label+=L"\r\nO teste terminou ou o parceiro saiu. Feche pelo menu e abra novamente para repetir.";
 else if(role==3&&local.phase==LocalPreviewPhase::path_limit)label+=L"\r\nParceiro pausado: volte para onde voce estava quando ele apareceu e espere 2 segundos.";
 else if(role==3&&local.phase==LocalPreviewPhase::stale_source)label+=L"\r\nTeste local aguardando movimentos atuais. Volte ao jogo para retomar.";
 else if(state.network==static_cast<unsigned>(State::listening))label+=role==3?L"\r\nPreparando o parceiro de teste neste PC...":L"\r\nAguardando o notebook. Nele, selecione este PC e clique em Entrar.";
 else if(state.network==static_cast<unsigned>(State::connected)){
  if(state.waiting==3)label+=L"\r\nAnde alguns passos em um computador para separar os dois jogadores.";
  else if(state.waiting==4)label+=L"\r\nFiquem no mesmo local seguro e perto um do outro.";
  else if(state.actor_phase==4)label+=state.paused?L"\r\nParceiro pausado. Volte ao jogo e solte os botoes para retomar.":(role==3?L"\r\nDiagnostico legado ativo.":L"\r\nControlador remoto ativo: teste caminhada, ataque e defesa.");
  else if(state.actor_phase==2||state.actor_phase==3)label+=L"\r\nPreparando o personagem do parceiro...";
  else label+=role==3?L"\r\nEm um trecho plano e vazio, pressione F8 neste jogo e ande poucos passos.":L"\r\nConectados. Carregue nos dois jogos, no mesmo trecho seguro; a criacao e automatica.";
 }else label+=L"\r\nCarregando a conexao...";
 if(advertiser&&advertiser->read().error)label+=L"\r\nNao foi possivel anunciar a sala. Confira se outro lancador esta aberto.";
 if(state.guard_rearm)label+=L"\r\nPara retomar a defesa, solte o botao e pressione novamente.";
 if(receipt_failed)label+=L"\r\nNao foi possivel salvar o registro do parceiro simulado; o jogo continua aberto.";
 std::wostringstream text;text<<label<<L"\r\nMovimentos enviados: "<<state.sent<<L"    recebidos: "<<state.received
  <<(role==3?L"\r\nAcoes locais registradas: ":L"\r\nComandos recebidos: ")<<(role==3?state.actions_captured:state.native_controller_commands)
  <<(role==3?L"    do parceiro aplicadas: ":L"    consumo nativo confirmado: ")<<(role==3?state.actions_replayed:state.native_controller_consumed);SetWindowTextW(status_box,text.str().c_str());
}
HWND control(const wchar_t* kind,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id=0){auto c=CreateWindowExW(0,kind,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return c;}
LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM w,LPARAM l){try{switch(message){
 case WM_CREATE:window=h;
  control(L"STATIC",L"Sekiro Co-op - assistente de testes",0,20,18,640,26);
  control(L"STATIC",L"Em rede: Steam aberta nos dois computadores, cada um na sua conta.\r\nPara testar sozinho, escolha um dos testes locais abaixo.",0,20,55,640,52);
  create_button=control(L"BUTTON",L"Criar jogo neste PC",WS_TABSTOP,20,120,230,36,1);
  preview_button=control(L"BUTTON",L"Testar ataques e defesa neste PC",WS_TABSTOP,270,120,390,36,4);
  native_button=control(L"BUTTON",L"Testar controlador nativo neste PC",WS_TABSTOP,20,166,640,36,5);
  control(L"STATIC",L"PCs disponiveis na sua rede:",0,20,218,640,24);
  rooms_box=control(L"LISTBOX",L"",WS_BORDER|LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,20,247,640,80,3);
  join_button=control(L"BUTTON",L"Entrar no PC selecionado",WS_TABSTOP,20,342,250,36,2);EnableWindow(join_button,FALSE);
  status_box=control(L"STATIC",L"Procurando salas automaticamente...\r\nNao e preciso copiar codigos nem alterar as propriedades da Steam.",0,20,397,640,168);
  instructions_box=control(L"STATIC",L"Teste LAN nativo: Criar neste PC; no outro, escolha a sala e Entrar.\r\nCarregue no mesmo trecho seguro. O parceiro aparece sem F8.\r\nTeste caminhada, ataque e defesa; solte os botoes depois de pausar.\r\nO save pessoal fica separado. Dano e campanha ainda nao sao compartilhados.",0,20,581,640,90);
  SetTimer(h,1,200,nullptr);return 0;
 case WM_TIMER:update();return 0;
 case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){if(LOWORD(w)==1)start(1);else if(LOWORD(w)==2)start(2);else if(LOWORD(w)==4)start(3);else if(LOWORD(w)==5)start(4);}return 0;
 case WM_CLOSE:if(busy&&done)update();if(busy){MessageBoxW(h,L"Feche o Sekiro pelo menu antes de fechar o lancador. Durante a copia inicial, aguarde a preparacao terminar.",L"Teste em andamento",MB_OK);return 0;}browser.reset();advertiser.reset();DestroyWindow(h);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 }}catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Sekiro Co-op",MB_OK|MB_ICONERROR);}return DefWindowProcW(h,message,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){try{
 int count{};auto** args=CommandLineToArgvW(GetCommandLineW(),&count);if(count==4&&std::wstring_view(args[1])==L"--check"){const fs::path input=args[2],output=args[3];LocalFree(args);return headless(input,output);}LocalFree(args);
 WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"SkCoopSimpleLauncher";if(!RegisterClassW(&cls))return 2;
 const auto h=CreateWindowExW(0,cls.lpszClassName,L"Sekiro Co-op",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,710,730,nullptr,nullptr,instance,nullptr);if(!h)return 2;ShowWindow(h,show);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(h,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}return static_cast<int>(m.wParam);
 }catch(...){return 2;}}
