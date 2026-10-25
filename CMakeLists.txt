cmake_minimum_required(VERSION 3.24)
project(SekiroSeamlessCoop VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This diagnostic scaffold requires Windows x64 and MSVC (Visual Studio 2022).")
endif()

include(CTest)
add_subdirectory(src/core)

add_library(skcoop_windows_options INTERFACE)
target_compile_definitions(skcoop_windows_options INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(skcoop_windows_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)

add_library(skcoop_launcher_support STATIC
  src/launcher/preflight.cpp
  src/launcher/runtime_loader.cpp
  src/launcher/process.cpp
  src/launcher/steam_install.cpp
  src/launcher/launch_settings.cpp)
target_include_directories(skcoop_launcher_support PUBLIC include src/launcher)
target_link_libraries(skcoop_launcher_support PUBLIC skcoop_windows_options bcrypt advapi32)

add_library(skcoop_actor_demo STATIC src/runtime/actor_demo.cpp)
target_include_directories(skcoop_actor_demo PUBLIC src/runtime)
target_link_libraries(skcoop_actor_demo PUBLIC skcoop_windows_options)
add_library(skcoop_runtime SHARED src/runtime/runtime.cpp src/runtime/launch_bootstrap.cpp src/runtime/actor_native.cpp src/runtime/runtime.def)
target_include_directories(skcoop_runtime PUBLIC include)
target_compile_definitions(skcoop_runtime PRIVATE SKCOOP_RUNTIME_BUILD)
target_link_libraries(skcoop_runtime PRIVATE skcoop_windows_options skcoop_core skcoop_actor_demo bcrypt user32)

add_library(skcoop_save_policy STATIC src/saves/save_policy.cpp)
target_include_directories(skcoop_save_policy PUBLIC include)
target_link_libraries(skcoop_save_policy PUBLIC skcoop_windows_options)

add_library(skcoop_save_native_policy STATIC src/saves/save_native_policy.cpp)
target_include_directories(skcoop_save_native_policy PUBLIC include)
target_link_libraries(skcoop_save_native_policy PUBLIC skcoop_save_policy)

# Unmodified Microsoft Detours 4.0.1, pinned provenance/license alongside source.
add_library(skcoop_detours STATIC
  third_party/detours/src/detours.cpp third_party/detours/src/modules.cpp
  third_party/detours/src/disasm.cpp third_party/detours/src/image.cpp
  third_party/detours/src/creatwth.cpp third_party/detours/src/disolx86.cpp
  third_party/detours/src/disolx64.cpp third_party/detours/src/disolia64.cpp
  third_party/detours/src/disolarm.cpp third_party/detours/src/disolarm64.cpp)
target_include_directories(skcoop_detours PUBLIC third_party/detours/src)
set_property(TARGET skcoop_detours PROPERTY CXX_STANDARD 14)
target_compile_definitions(skcoop_detours PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
target_compile_options(skcoop_detours PRIVATE /W4 /Gy /Zl)

add_library(skcoop_save_bootstrap SHARED
  src/save_bootstrap/bootstrap_dll.cpp src/save_bootstrap/bootstrap.def)
target_include_directories(skcoop_save_bootstrap PRIVATE include)
target_link_libraries(skcoop_save_bootstrap PRIVATE skcoop_windows_options skcoop_detours)
add_library(skcoop_bootstrap_host STATIC src/save_bootstrap/bootstrap_host.cpp)
target_include_directories(skcoop_bootstrap_host PUBLIC include)
target_link_libraries(skcoop_bootstrap_host PUBLIC skcoop_windows_options skcoop_detours bcrypt)

add_library(skcoop_save_settings STATIC src/saves/save_settings.cpp)
target_include_directories(skcoop_save_settings PUBLIC include)
target_link_libraries(skcoop_save_settings PUBLIC skcoop_save_policy)

add_library(skcoop_save_store STATIC src/saves/save_store.cpp)
target_include_directories(skcoop_save_store PUBLIC include)
target_link_libraries(skcoop_save_store PUBLIC skcoop_windows_options)

add_library(skcoop_save_native_io STATIC src/saves/native_io_hooks.cpp)
target_include_directories(skcoop_save_native_io PUBLIC include src/saves)
target_link_libraries(skcoop_save_native_io PUBLIC skcoop_save_native_policy skcoop_save_store skcoop_detours shell32)
target_link_libraries(skcoop_save_bootstrap PRIVATE skcoop_save_native_io)
target_link_libraries(skcoop_runtime PRIVATE skcoop_save_native_io)

add_library(skcoop_save_bridge STATIC src/saves/save_bridge.cpp)
target_include_directories(skcoop_save_bridge PUBLIC include)
target_link_libraries(skcoop_save_bridge PUBLIC skcoop_save_settings skcoop_save_store)

add_executable(skcoop_launcher src/launcher/main.cpp src/launcher/protected_launch.cpp)
target_link_libraries(skcoop_launcher PRIVATE skcoop_launcher_support skcoop_save_settings skcoop_save_store skcoop_detours shell32)
add_dependencies(skcoop_launcher skcoop_runtime)
add_custom_target(skcoop_prepare_config ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:skcoop_launcher>/config"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/config/game_profiles.ini"
    "$<TARGET_FILE_DIR:skcoop_launcher>/config/game_profiles.ini"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/config/save_profile.ini"
    "$<TARGET_FILE_DIR:skcoop_launcher>/config/save_profile.ini"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/game_profiles.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/save_profile.ini"
  VERBATIM)
add_dependencies(skcoop_launcher skcoop_prepare_config)

add_library(skcoop_probe_policy STATIC src/probe/policy.cpp)
target_include_directories(skcoop_probe_policy PUBLIC src/probe)
target_link_libraries(skcoop_probe_policy PUBLIC skcoop_launcher_support)
add_executable(skcoop_probe src/probe/main.cpp)
target_link_libraries(skcoop_probe PRIVATE skcoop_probe_policy psapi)

if(BUILD_TESTING)
  add_executable(skcoop_actor_demo_tests tests/actor_demo_tests.cpp)
  target_link_libraries(skcoop_actor_demo_tests PRIVATE skcoop_actor_demo)
  add_test(NAME actor_demo_lifecycle_tests COMMAND skcoop_actor_demo_tests)
  add_executable(skcoop_runtime_launch_guard_tests tests/runtime_launch_guard_tests.cpp)
  target_include_directories(skcoop_runtime_launch_guard_tests PRIVATE include)
  target_link_libraries(skcoop_runtime_launch_guard_tests PRIVATE skcoop_windows_options skcoop_detours)
  add_dependencies(skcoop_runtime_launch_guard_tests skcoop_runtime)
  add_test(NAME runtime_launch_guard_tests COMMAND skcoop_runtime_launch_guard_tests "$<TARGET_FILE:skcoop_runtime>")
  set_tests_properties(runtime_launch_guard_tests PROPERTIES TIMEOUT 35)
  add_executable(skcoop_steam_install_tests tests/steam_install_tests.cpp)
  target_link_libraries(skcoop_steam_install_tests PRIVATE skcoop_launcher_support)
  add_test(NAME steam_install_discovery_tests COMMAND skcoop_steam_install_tests)
  set_tests_properties(steam_install_discovery_tests PROPERTIES TIMEOUT 20)
  add_executable(skcoop_launch_settings_tests tests/launch_settings_tests.cpp)
  target_link_libraries(skcoop_launch_settings_tests PRIVATE skcoop_launcher_support)
  add_test(NAME launch_settings_tests COMMAND skcoop_launch_settings_tests)
  set_tests_properties(launch_settings_tests PROPERTIES TIMEOUT 15)
  add_executable(skcoop_owned_job_tests tests/owned_job_tests.cpp)
  target_link_libraries(skcoop_owned_job_tests PRIVATE skcoop_launcher_support)
  add_test(NAME owned_job_lifecycle_tests COMMAND skcoop_owned_job_tests)
  set_tests_properties(owned_job_lifecycle_tests PROPERTIES TIMEOUT 30)
  add_executable(skcoop_save_bootstrap_fixture tests/save_bootstrap_fixture.cpp)
  target_include_directories(skcoop_save_bootstrap_fixture PRIVATE include)
  target_link_libraries(skcoop_save_bootstrap_fixture PRIVATE skcoop_windows_options shell32)
  add_executable(skcoop_save_bootstrap_tests tests/save_bootstrap_tests.cpp)
  target_link_libraries(skcoop_save_bootstrap_tests PRIVATE skcoop_bootstrap_host)
  add_dependencies(skcoop_save_bootstrap_tests skcoop_save_bootstrap skcoop_save_bootstrap_fixture)
  add_test(NAME save_early_bootstrap_tests COMMAND skcoop_save_bootstrap_tests
    "$<TARGET_FILE:skcoop_save_bootstrap_fixture>" "$<TARGET_FILE:skcoop_save_bootstrap>")
  set_tests_properties(save_early_bootstrap_tests PROPERTIES TIMEOUT 80)
  add_executable(skcoop_windows_smoke tests/windows_smoke.cpp)
  target_link_libraries(skcoop_windows_smoke PRIVATE skcoop_launcher_support)
  add_dependencies(skcoop_windows_smoke skcoop_runtime skcoop_launcher)
  add_test(NAME windows_runtime_smoke COMMAND skcoop_windows_smoke
    --runtime "$<TARGET_FILE:skcoop_runtime>" --launcher "$<TARGET_FILE:skcoop_launcher>")
  set_tests_properties(windows_runtime_smoke PROPERTIES TIMEOUT 30)
  add_executable(skcoop_probe_tests tests/probe_tests.cpp)
  target_link_libraries(skcoop_probe_tests PRIVATE skcoop_probe_policy)
  add_dependencies(skcoop_probe_tests skcoop_probe)
  add_test(NAME probe_policy_tests COMMAND skcoop_probe_tests "$<TARGET_FILE:skcoop_probe>")
  set_tests_properties(probe_policy_tests PROPERTIES TIMEOUT 15)

  add_executable(skcoop_save_policy_tests tests/save_policy_tests.cpp)
  target_link_libraries(skcoop_save_policy_tests PRIVATE skcoop_save_policy)
  add_test(NAME save_path_policy_tests COMMAND skcoop_save_policy_tests)
  set_tests_properties(save_path_policy_tests PROPERTIES TIMEOUT 15)

  add_executable(skcoop_save_native_policy_tests tests/save_native_policy_tests.cpp)
  target_link_libraries(skcoop_save_native_policy_tests PRIVATE skcoop_save_native_policy)
  add_test(NAME save_native_path_policy_tests COMMAND skcoop_save_native_policy_tests)
  set_tests_properties(save_native_path_policy_tests PROPERTIES TIMEOUT 15)

  add_executable(skcoop_save_settings_tests tests/save_settings_tests.cpp)
  target_link_libraries(skcoop_save_settings_tests PRIVATE skcoop_save_settings)
  add_test(NAME save_settings_tests COMMAND skcoop_save_settings_tests)
  set_tests_properties(save_settings_tests PROPERTIES TIMEOUT 15)

  add_executable(skcoop_save_store_tests tests/save_store_tests.cpp)
  target_link_libraries(skcoop_save_store_tests PRIVATE skcoop_save_store)
  add_test(NAME save_storage_tests COMMAND skcoop_save_store_tests)
  set_tests_properties(save_storage_tests PROPERTIES TIMEOUT 45)

  add_executable(skcoop_save_bridge_tests tests/save_bridge_tests.cpp)
  target_link_libraries(skcoop_save_bridge_tests PRIVATE skcoop_save_bridge)
  add_test(NAME save_profile_bridge_tests COMMAND skcoop_save_bridge_tests)
  set_tests_properties(save_profile_bridge_tests PROPERTIES TIMEOUT 30)
endif()
