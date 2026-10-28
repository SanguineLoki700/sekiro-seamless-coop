#pragma once
#include <type_traits>
namespace skcoop::actors {
// /EHsc RAII alone does not unwind asynchronous native SEH. Keep the native
// call in this narrow helper, with no objects needing C++ destruction, so the
// TLS selection is restored on normal return, nested calls, C++ and SEH exits.
template<class Scope,class Function,class... Args>
void invoke_native_scoped(Scope*& selection,Scope& current,Function function,Args... args) {
    static_assert(std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>);
    static_assert((std::is_trivially_destructible_v<Args> && ...));
    static_assert(std::is_void_v<std::invoke_result_t<Function,Args...>>);
    auto* previous=selection;selection=&current;
    __try {function(args...);}
    __finally {selection=previous;}
}
}
