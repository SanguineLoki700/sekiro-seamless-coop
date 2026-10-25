// Portable connection check. Probe traffic can never control a native avatar.
#include <Windows.h>
#include <shellapi.h>
#include "skcoop/lan/peer.hpp"
#include "protected_launch.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <atomic>
using namespace skcoop::lan;
namespace {
HWND window{},address_box{},code_box{},status_box{},host_button{},join_button{},stop_button{},copy_button{},role_box{},prepare_button{},steam_box{},steam_button{};
std::unique_ptr<Peer> peer;Ticket ticket;Status last_status{};std::uint64_t connected_at{};bool passed{};
std::thread prepare_thread;std::atomic<bool> preparing{},prepare_done{};std::wstring prepare_result,prepare_error;
std::filesystem::path directory(){std::wstring s(32768,0);const auto n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));if(!n||n==s.size())throw std::runtime_error("Cannot find package folder");s.resize(n);return std::filesystem::path(s).parent_path();}
std::wstring wide(std::string_view s){return {s.begin(),s.end()};}
std::wstring text(HWND h){const auto n=GetWindowTextLengthW(h);if(n>1024)throw std::runtime_error("Text exceeds limit");std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
std::string ascii(HWND h){const auto s=text(h);if(std::any_of(s.begin(),s.end(),[](auto c){return c<32||c>126;}))throw std::runtime_error("Invalid ASCII value");std::string out;for(auto c:s)out.push_back(static_cast<char>(c));return out;}
void copy(std::wstring_view s){if(!OpenClipboard(window))throw std::runtime_error("Clipboard unavailable");
 const auto h=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));if(!h){CloseClipboard();throw std::bad_alloc();}
 auto* p=GlobalLock(h);if(!p){GlobalFree(h);CloseClipboard();throw std::runtime_error("Clipboard allocation unavailable");}
 std::memcpy(p,s.data(),s.size()*sizeof(wchar_t));static_cast<wchar_t*>(p)[s.size()]=0;GlobalUnlock(h);
 if(!EmptyClipboard()||!SetClipboardData(CF_UNICODETEXT,h)){GlobalFree(h);CloseClipboard();throw std::runtime_error("Clipboard write failed");}CloseClipboard();
}
SessionConfig config(Role role,const Ticket& t){SessionConfig c;c.role=role;c.ticket=t;c.participant=random_participant();return c;}
Motion probe_motion(){Motion m;m.epoch=1;return m;}
bool good(const Status& s){return s.state==State::connected&&s.frames_sent>=20&&s.frames_received>=20;}
void write_report(const std::filesystem::path& path,const Status& s,bool success){
 std::ostringstream out;out<<"{\"kind\":\"lan-connection-check\",\"wire_protocol\":1,\"native_game_test\":false,\"passed\":"<<(success?"true":"false")
 <<",\"state\":"<<static_cast<int>(s.state)<<",\"error\":"<<static_cast<int>(s.failure)<<",\"system_error\":"<<s.system_error
 <<",\"frames_sent\":"<<s.frames_sent<<",\"frames_received\":"<<s.frames_received<<",\"rejected\":"<<s.rejected<<"}\n";
 const auto value=out.str();HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create new report");DWORD written{};const bool ok=WriteFile(h,value.data(),static_cast<DWORD>(value.size()),&written,nullptr)&&written==value.size()&&FlushFileBuffers(h);CloseHandle(h);if(!ok)throw std::runtime_error("Report write failed");
}
int headless(const std::filesystem::path& path,const std::filesystem::path& report){
 std::ifstream in(path,std::ios::binary);std::string role,code,extra;std::getline(in,role);std::getline(in,code);
 if(!in||std::getline(in,extra)||code.size()>160||(role!="host"&&role!="guest"))return 2;
 const auto t=parse_ticket(code);if(!t)return 2;Peer p(config(role=="host"?Role::host:Role::guest,*t));const auto start=GetTickCount64();Status s;
 bool success=false;Status confirmed;
 while(GetTickCount64()-start<12000){p.try_offer(probe_motion());s=p.read().status;if(good(s)){success=true;confirmed=s;if(GetTickCount64()-start>=4000)break;}
  if(s.state==State::failed||s.state==State::lost)break;Sleep(20);}
 p.stop();write_report(report,success?confirmed:s,success);return success?0:1;
}
void controls(bool active){EnableWindow(host_button,!active);EnableWindow(join_button,!active);EnableWindow(address_box,!active);EnableWindow(stop_button,active);SendMessageW(code_box,EM_SETREADONLY,active,0);}
void start(Role role){if(peer)return;
 if(role==Role::host){ticket={ascii(address_box),24860,random_id(),random_key()};if(!valid_ipv4(ticket.address))throw std::runtime_error("Escolha seu IPv4 na lista.");SetWindowTextW(code_box,wide(ticket_text(ticket)).c_str());}
 else{const auto parsed=parse_ticket(ascii(code_box));if(!parsed)throw std::runtime_error("Codigo invalido. Copie o codigo inteiro do PC.");ticket=*parsed;}
 peer=std::make_unique<Peer>(config(role,ticket));passed=false;connected_at=0;controls(true);
}
void prepare(){if(preparing)return;
 const auto role=SendMessageW(role_box,CB_GETCURSEL,0,0)==0?1u:2u;
 auto code=ascii(code_box);
 if(role==1){ticket={ascii(address_box),24860,random_id(),random_key()};if(!valid_ipv4(ticket.address))throw std::runtime_error("Escolha seu IPv4.");code=ticket_text(ticket);SetWindowTextW(code_box,wide(code).c_str());}
 if(!parse_ticket(code))throw std::runtime_error("Cole o novo codigo do PC antes de preparar o notebook.");
 if(peer){peer->stop();peer.reset();}controls(false);EnableWindow(host_button,FALSE);EnableWindow(join_button,FALSE);EnableWindow(prepare_button,FALSE);EnableWindow(role_box,FALSE);EnableWindow(address_box,FALSE);SendMessageW(code_box,EM_SETREADONLY,TRUE,0);
 prepare_result.clear();prepare_error.clear();preparing=true;prepare_done=false;const auto internal=directory()/L"SeamlessCoop";
 SetWindowTextW(status_box,L"Preparando a copia protegida. Isto pode copiar cerca de 25 GB.\r\nAguarde; nao abra o Sekiro durante esta preparacao.");
 prepare_thread=std::thread([role,code,internal]{try{prepare_result=skcoop::launcher::prepare_lan_test(internal,role,code);}catch(const std::exception& e){prepare_error=wide(e.what());}catch(...){prepare_error=L"Falha ao preparar o teste.";}prepare_done=true;});
}
void update(){if(preparing){if(!prepare_done)return;prepare_thread.join();preparing=false;controls(false);EnableWindow(prepare_button,TRUE);EnableWindow(role_box,TRUE);
 if(!prepare_error.empty()){SetWindowTextW(status_box,L"Preparacao interrompida. Nenhum save existente foi substituido.");MessageBoxW(window,prepare_error.c_str(),L"Preparacao",MB_OK|MB_ICONERROR);}
 else{SetWindowTextW(steam_box,prepare_result.c_str());EnableWindow(steam_button,TRUE);SetWindowTextW(status_box,L"Copia e perfil preparados. Copie a linha abaixo para as opcoes da Steam.\r\nNo PC, compartilhe o NOVO codigo acima; no notebook, cole-o e prepare tambem.\r\nO primeiro teste no jogo precisa do mesmo save inicial nos dois computadores.");}return;}
 if(!peer)return;peer->try_offer(probe_motion());last_status=peer->read().status;const auto& s=last_status;
 if(s.state==State::connected&&!connected_at)connected_at=GetTickCount64();
 passed=good(s)&&connected_at&&GetTickCount64()-connected_at>=1000;
 const wchar_t* label=L"Preparando...";
 switch(s.state){case State::listening:label=L"Aguardando o notebook. Copie o codigo e use Entrar nele.";break;
 case State::connecting:label=L"Conectando ao PC...";break;case State::handshaking:label=L"Conferindo o codigo e a sessao...";break;
 case State::connected:label=passed?L"Conexao verificada nos dois sentidos. Isto ainda nao testa o jogo.":L"Conectado. Verificando a troca de dados...";break;
 case State::lost:label=L"A conexao foi interrompida. Pare e tente de novo nos dois computadores.";break;
 case State::failed:label=L"Falha de conexao. Confira rede, codigo e permissao do Firewall para rede privada.";break;
 case State::stopped:label=L"Teste encerrado.";break;default:break;}
 std::wostringstream out;out<<label<<L"\r\n\r\nDados enviados: "<<s.frames_sent<<L"    recebidos: "<<s.frames_received;
 if(s.system_error)out<<L"\r\nCodigo do Windows: "<<s.system_error;SetWindowTextW(status_box,out.str().c_str());
}
HWND control(const wchar_t* type,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id=0){auto hctl=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(hctl,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return hctl;}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l){try{
 switch(msg){case WM_CREATE:{window=h;
  control(L"STATIC",L"Teste de conexao LAN - Sekiro Co-op",0,20,16,640,25);
  control(L"STATIC",L"No PC: escolha seu IPv4 e clique em Criar. No notebook: cole o codigo e clique em Entrar.",0,20,48,650,36);
  address_box=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,20,96,220,180);
  for(const auto& ip:local_addresses())SendMessageW(address_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(ip).c_str()));SendMessageW(address_box,CB_SETCURSEL,0,0);
  host_button=control(L"BUTTON",L"Criar no PC",WS_TABSTOP,254,94,130,28,1);
  control(L"STATIC",L"Codigo da sala (compartilhe apenas com quem vai testar):",0,20,140,620,20);
  code_box=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,20,166,650,26);SendMessageW(code_box,EM_SETLIMITTEXT,159,0);
  copy_button=control(L"BUTTON",L"Copiar codigo",WS_TABSTOP,20,204,135,28,3);join_button=control(L"BUTTON",L"Entrar no notebook",WS_TABSTOP,168,204,160,28,2);
  stop_button=control(L"BUTTON",L"Parar",WS_TABSTOP,342,204,100,28,4);EnableWindow(stop_button,FALSE);
  control(L"BUTTON",L"Salvar resultado",WS_TABSTOP,456,204,150,28,5);
  status_box=control(L"STATIC",L"Este verificador funciona sem Sekiro ou Steam.\r\nSe o Firewall perguntar, permita apenas na sua rede privada.",0,20,256,650,105);
  control(L"STATIC",L"Prototipo de rede para 2 computadores. O teste de personagens no jogo e separado.",0,20,374,650,30);
  control(L"STATIC",L"Com o Sekiro instalado e um save normal carregado e fechado antes:",0,20,414,650,22);
  role_box=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,20,446,185,100);for(const auto* r:{L"PC (criar sala)",L"Notebook (entrar)"})SendMessageW(role_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(r));SendMessageW(role_box,CB_SETCURSEL,0,0);
  prepare_button=control(L"BUTTON",L"Preparar teste no jogo",WS_TABSTOP,220,444,220,28,6);
  steam_box=control(L"EDIT",L"",WS_BORDER|ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,20,488,650,26);
  steam_button=control(L"BUTTON",L"Copiar linha da Steam",WS_TABSTOP,20,526,200,28,7);EnableWindow(steam_button,FALSE);
  control(L"STATIC",L"A Steam continua responsavel por abrir e autenticar o jogo. Ataques ainda nao sao sincronizados.",0,20,570,650,35);
  SetTimer(h,1,100,nullptr);return 0;}
 case WM_COMMAND:if(HIWORD(w)==BN_CLICKED)switch(LOWORD(w)){case 1:start(Role::host);break;case 2:start(Role::guest);break;case 3:copy(text(code_box));break;
  case 4:if(peer){peer->stop();peer.reset();controls(false);SetWindowTextW(status_box,L"Parado. Crie uma sala nova para repetir.");}break;
  case 5:{if(!peer)throw std::runtime_error("Execute o teste antes de salvar.");const auto path=std::filesystem::current_path()/(L"resultado-lan-"+std::to_wstring(GetTickCount64())+L".json");write_report(path,last_status,passed);MessageBoxW(h,L"Resultado salvo na pasta do verificador. O codigo da sala nao foi incluido.",L"Resultado",MB_OK);break;}
  case 6:prepare();break;case 7:copy(text(steam_box));break;
 }return 0;
 case WM_TIMER:update();return 0;case WM_CLOSE:if(preparing){MessageBoxW(h,L"Aguarde a preparacao terminar antes de fechar.",L"Preparando",MB_OK);return 0;}if(peer){peer->stop();peer.reset();}DestroyWindow(h);return 0;case WM_DESTROY:PostQuitMessage(0);return 0;
 }
 }catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Teste LAN",MB_OK|MB_ICONERROR);}return DefWindowProcW(h,msg,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){try{int count{};auto** args=CommandLineToArgvW(GetCommandLineW(),&count);
 if(count==5&&std::wstring_view(args[1])==L"--prepare"){
  const auto role=std::wstring_view(args[2])==L"host"?1u:std::wstring_view(args[2])==L"guest"?2u:0u;
  std::ifstream f(std::filesystem::path(args[3]),std::ios::binary);std::string code;std::getline(f,code);if(!f||code.size()>159||f.peek()!=EOF){LocalFree(args);return 2;}
  const auto output=std::filesystem::path(args[4]);LocalFree(args);std::wstring result;
  try{result=skcoop::launcher::prepare_lan_test(directory()/L"SeamlessCoop",role,code);}catch(const std::exception& e){
   const auto error_path=output.wstring()+L".error.txt";HANDLE file=CreateFileW(error_path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
   if(file!=INVALID_HANDLE_VALUE){DWORD written{};WriteFile(file,e.what(),static_cast<DWORD>(std::strlen(e.what())),&written,nullptr);CloseHandle(file);}return 2;}
  HANDLE file=CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return 2;DWORD written{};const auto n=static_cast<DWORD>(result.size()*sizeof(wchar_t));const bool ok=WriteFile(file,result.data(),n,&written,nullptr)&&written==n;CloseHandle(file);return ok?0:2;
 }
 if(count==4&&std::wstring_view(args[1])==L"--check"){const auto input=std::filesystem::path(args[2]),output=std::filesystem::path(args[3]);LocalFree(args);return headless(input,output);}LocalFree(args);
 WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"SkCoopLanCheck";
 if(!RegisterClassW(&cls))return 2;const auto h=CreateWindowExW(0,cls.lpszClassName,L"Sekiro Co-op - teste LAN",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,710,660,nullptr,nullptr,instance,nullptr);
 if(!h)return 2;ShowWindow(h,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}return static_cast<int>(msg.wParam);
 }catch(...){return 2;}}
