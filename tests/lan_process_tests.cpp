#include <Windows.h>
#include "skcoop/lan/protocol.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void require(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
struct Child {
 HANDLE process{};DWORD pid{};
 Child(const fs::path& exe,const fs::path& input,const fs::path& report){
  auto command=L"\""+exe.wstring()+L"\" --check \""+input.wstring()+L"\" \""+report.wstring()+L"\"";STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION info{};
  require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&info)!=FALSE,"Cannot create owned checker");
  process=info.hProcess;pid=info.dwProcessId;CloseHandle(info.hThread);
 }
 DWORD result(){require(WaitForSingleObject(process,16000)==WAIT_OBJECT_0,"Checker timed out");DWORD code{};require(GetExitCodeProcess(process,&code)!=FALSE,"Missing checker result");return code;}
 ~Child(){if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,2);WaitForSingleObject(process,3000);}CloseHandle(process);}}
};
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int wmain(int argc,wchar_t** argv){fs::path root;try{
 require(argc==2,"Expected checker path");root=fs::temp_directory_path()/(L"skcoop-lan-check-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));require(fs::create_directory(root),"Cannot create test directory");
 const skcoop::lan::Ticket ticket{"127.0.0.1",static_cast<std::uint16_t>(30000+GetCurrentProcessId()%20000),skcoop::lan::random_id(),skcoop::lan::random_key()};const auto code=skcoop::lan::ticket_text(ticket);
 {std::ofstream f(root/L"host.txt",std::ios::binary);f<<"host\n"<<code<<'\n';}
 {std::ofstream f(root/L"guest.txt",std::ios::binary);f<<"guest\n"<<code<<'\n';}
 {Child host(argv[1],root/L"host.txt",root/L"host.json");Sleep(150);Child guest(argv[1],root/L"guest.txt",root/L"guest.json");
  require(host.pid!=guest.pid&&host.pid!=GetCurrentProcessId(),"Processes must be distinct");
  require(host.result()==0&&guest.result()==0,"Both independent checker processes must pass");}
 for(const auto* name:{L"host.json",L"guest.json"}){const auto s=read(root/name);require(s.find("\"passed\":true")!=std::string::npos&&s.find("\"native_game_test\":false")!=std::string::npos,"Missing independent report");require(s.find(code)==std::string::npos&&s.find("SKL1|")==std::string::npos,"Room credential leaked into report");}
 // A one-byte-damaged credential cannot yield a successful report.
 auto damaged=code;damaged.back()=damaged.back()=='1'?'2':'1';{std::ofstream f(root/L"bad.txt",std::ios::binary);f<<"guest\n"<<damaged<<'\n';}
 {Child host(argv[1],root/L"host.txt",root/L"bad-host.json");Sleep(150);Child guest(argv[1],root/L"bad.txt",root/L"bad-guest.json");require(host.result()!=0&&guest.result()!=0,"Wrong credential must fail both checker processes");}
 fs::remove_all(root);std::cout<<"PASS separate checker processes, bidirectional traffic, independent reports and wrong-key refusal\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(!root.empty())fs::remove_all(root);return 1;}}
