#include "native_call_scope.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
using namespace skcoop::actors;
namespace {
struct Scope {int value;};
thread_local Scope* selected{};
constexpr DWORD owned_exception=0xe0424313;
void require(bool okay){if(!okay)throw std::runtime_error("Native scope cleanup regression");}
void check_current(int value){require(selected&&selected->value==value);}
void throw_cpp(){check_current(2);throw std::runtime_error("owned C++ fixture");}
void throw_seh(){check_current(2);RaiseException(owned_exception,0,0,nullptr);}
bool run_seh(Scope& current){
    __try {invoke_native_scoped(selected,current,&throw_seh);}
    __except(GetExceptionCode()==owned_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
void nested(){Scope current{3};invoke_native_scoped(selected,current,&check_current,3);check_current(2);}
}
int main(){try{
    Scope outer{1},inner{2};selected=&outer;
    invoke_native_scoped(selected,inner,&check_current,2);require(selected==&outer);
    invoke_native_scoped(selected,inner,&nested);require(selected==&outer);
    bool caught=false;try{invoke_native_scoped(selected,inner,&throw_cpp);}catch(const std::runtime_error&){caught=true;}
    require(caught&&selected==&outer);
    require(run_seh(inner)&&selected==&outer);
    selected=nullptr;invoke_native_scoped(selected,inner,&check_current,2);require(!selected);
    std::cout<<"PASS native TLS scope cleanup on return, nesting, C++ and owned SEH exceptions; no game execution\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
