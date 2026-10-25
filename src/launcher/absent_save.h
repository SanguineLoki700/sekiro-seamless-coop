#pragma once
#include "file_open_error.h"
#include <cwchar>
namespace skcoop::launcher {
// A nonexistent file cannot be locked without creating it. Keep its canonical
// parent pinned, never create a placeholder, and fail the run if it appears.
class AbsentSave {
 struct Pin {HANDLE h{INVALID_HANDLE_VALUE};~Pin(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} parent_;
 std::filesystem::path path_;
public:
 explicit AbsentSave(const std::filesystem::path& path):path_(std::filesystem::absolute(path).lexically_normal()){
  path_.make_preferred();const auto leaf=path_.filename().wstring();
  if(leaf!=L"S0000.sl2"&&leaf!=L"S0000.sl2.bak")throw std::invalid_argument("Unexpected original save leaf");
  const auto directory=path_.parent_path();parent_.h=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES|FILE_TRAVERSE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(parent_.h==INVALID_HANDLE_VALUE)throw FileOpenError(directory,GetLastError());BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(parent_.h,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Original parent is not a regular directory");
  std::wstring resolved(32768,0);const auto n=GetFinalPathNameByHandleW(parent_.h,resolved.data(),static_cast<DWORD>(resolved.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
  if(n<=4||n>=resolved.size())throw std::runtime_error("Cannot resolve original parent");resolved.resize(n);
  if(!resolved.starts_with(L"\\\\?\\")||_wcsicmp(resolved.c_str()+4,directory.c_str()))throw std::runtime_error("Original parent alias rejected");
  if(!still_absent())throw std::runtime_error("Expected original save is not absent");
 }
 AbsentSave(const AbsentSave&)=delete;AbsentSave& operator=(const AbsentSave&)=delete;
 bool still_absent()const noexcept{return GetFileAttributesW(path_.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND;}
 HANDLE parent_handle()const noexcept{return parent_.h;}
};
}
