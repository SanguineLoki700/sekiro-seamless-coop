#include "protected_launch.h"
#include "file_open_error.h"
#include "skcoop/lan/protocol.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace skcoop::launcher;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void missing_or_locked(const fs::path& internal,const std::string& code,const fs::path& expected,DWORD error){
 try{(void)prepare_lan_test(internal,1,code);throw std::runtime_error("Preparation unexpectedly succeeded");}
 catch(const FileOpenError& e){
  require(e.selected_path==expected&&e.system_error==error,"Wrong failed path or Windows error");
  require(diagnostic_wide(e.what()).find(expected.wstring())!=std::wstring::npos,"GUI diagnostic lost the exact Unicode path");
  require(std::string(e.what()).find(code)==std::string::npos,"Room credential leaked into file error");
 }
}
int wmain(int argc,wchar_t** argv){fs::path root;HANDLE lock=INVALID_HANDLE_VALUE;try{
 require(argc==2,"Expected compiled runtime path");root=fs::temp_directory_path()/(L"skcoop-prep-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-\u00e9");
 require(fs::create_directory(root),"Cannot create fixture");
 const auto code=skcoop::lan::ticket_text({"127.0.0.1",24860,skcoop::lan::random_id(),skcoop::lan::random_key()});
 const auto runtime=root/L"skcoop_runtime.dll";
 missing_or_locked(root,code,runtime,ERROR_FILE_NOT_FOUND);
 {std::ofstream f(runtime,std::ios::binary);f<<"owned file must remain intact";}
 lock=CreateFileW(runtime.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(lock!=INVALID_HANDLE_VALUE,"Cannot lock fixture");
 missing_or_locked(root,code,runtime,ERROR_SHARING_VIOLATION);char buffer[64]{};DWORD n{};
 require(ReadFile(lock,buffer,sizeof(buffer),&n,nullptr)&&std::string(buffer,n)=="owned file must remain intact","Preparation changed the locked fixture");CloseHandle(lock);lock=INVALID_HANDLE_VALUE;
 fs::copy_file(argv[1],runtime,fs::copy_options::overwrite_existing);
 missing_or_locked(root,code,root/L"skcoop_launcher.exe",ERROR_FILE_NOT_FOUND);
 require(std::distance(fs::directory_iterator(root),fs::directory_iterator{})==1,"Failed preparation wrote files before a valid package existed");
 fs::remove_all(root);std::cout<<"PASS actual preparation failures preserve Unicode paths, Windows errors, locks and package state\n";return 0;
 }catch(const std::exception& e){if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);if(!root.empty())fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}}
