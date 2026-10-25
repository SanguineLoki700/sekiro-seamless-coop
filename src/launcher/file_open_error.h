#pragma once
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
namespace skcoop::launcher {
inline std::string diagnostic_utf8(std::wstring_view value){
 if(value.empty())return {};
 const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
 if(n<=0)throw std::runtime_error("Cannot encode diagnostic path");std::string out(n,0);
 if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr)!=n)throw std::runtime_error("Cannot encode diagnostic path");return out;
}
inline std::wstring diagnostic_wide(std::string_view value){
 if(value.empty())return {};
 const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
 if(n<=0)return L"Nao foi possivel mostrar a mensagem de erro.";std::wstring out(n,0);
 if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n)!=n)return L"Nao foi possivel mostrar a mensagem de erro.";return out;
}
class FileOpenError final:public std::runtime_error {
 static std::string describe(const std::filesystem::path& path,DWORD code){
  const char* reason="O Windows nao conseguiu abrir o arquivo.";
  if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)reason="Arquivo ou pasta nao encontrado.";
  else if(code==ERROR_SHARING_VIOLATION||code==ERROR_LOCK_VIOLATION)reason="O arquivo esta em uso por outro programa.";
  else if(code==ERROR_ACCESS_DENIED)reason="O Windows recusou o acesso ao arquivo.";
  std::string out="Nao foi possivel abrir este arquivo:\n"+diagnostic_utf8(path.wstring())+"\n\n"+reason+" (Windows "+std::to_string(code)+").";
  if((code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)&&path.filename()==L"S0000.sl2.bak")out+="\n\nO backup original nao foi encontrado neste computador.";
  else if((code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)&&path.filename()==L"S0000.sl2")out+="\n\nO save original da conta selecionada na Steam nao foi encontrado neste computador.";
  else if((code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)&&(path.filename()==L"skcoop_runtime.dll"||path.filename()==L"skcoop_launcher.exe"))out+="\n\nExtraia o pacote inteiro: o executavel e a pasta SeamlessCoop devem ficar juntos.";
  return out;
 }
public:
 FileOpenError(const std::filesystem::path& path,DWORD code):std::runtime_error(describe(path,code)),selected_path(path),system_error(code){}
 std::filesystem::path selected_path;DWORD system_error;
};
}
