#include "absent_save.h"
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){fs::path root;bool owned=false;try{
 const auto temporary=fs::temp_directory_path();HANDLE h=CreateFileW(temporary.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);require(h!=INVALID_HANDLE_VALUE,"Cannot inspect owned temporary parent");
 std::wstring physical(32768,0);const auto n=GetFinalPathNameByHandleW(h,physical.data(),static_cast<DWORD>(physical.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);CloseHandle(h);require(n>4&&n<physical.size(),"Cannot resolve temporary parent");physical.resize(n);require(physical.starts_with(L"\\\\?\\"),"Unexpected temporary parent");physical.erase(0,4);
 root=fs::path(physical)/(L"skcoop-absent-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));owned=fs::create_directory(root);require(owned,"Cannot create owned fixture");
 const auto primary=root/L"S0000.sl2",backup=root/L"S0000.sl2.bak";
 {skcoop::launcher::AbsentSave first(primary),second(backup);require(first.still_absent()&&second.still_absent()&&fs::is_empty(root),"Missing guards created placeholders");
  require(!RemoveDirectoryW(root.c_str()),"Original parent must stay pinned");
  {std::ofstream f(primary,std::ios::binary);f<<"external change must survive";}
  require(!first.still_absent()&&second.still_absent(),"Unexpected original creation was not detected");
  bool refused=false;try{skcoop::launcher::AbsentSave existing(primary);}catch(const std::exception&){refused=true;}require(refused,"Existing original adopted as missing");
 }
 std::ifstream f(primary,std::ios::binary);const std::string value{std::istreambuf_iterator<char>(f),{}};f.close();require(value=="external change must survive","Guard changed or deleted an external file");
 require(DeleteFileW(primary.c_str())&&RemoveDirectoryW(root.c_str()),"Cannot clean owned fixture");
 std::cout<<"PASS missing saves remain absent, parent pin, unexpected creation detection and external data preservation\n";return 0;
 }catch(const std::exception& e){if(owned&&!root.empty()){DeleteFileW((root/L"S0000.sl2").c_str());DeleteFileW((root/L"S0000.sl2.bak").c_str());RemoveDirectoryW(root.c_str());}std::cerr<<e.what()<<'\n';return 1;}}
