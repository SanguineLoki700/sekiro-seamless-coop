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
  src/launcher/process.cpp)
target_include_directories(skcoop_launcher_support PUBLIC include src/launcher)
target_link_libraries(skcoop_launcher_support PUBLIC skcoop_windows_options bcrypt)

add_library(skcoop_runtime SHARED src/runtime/runtime.cpp)
target_include_directories(skcoop_runtime PUBLIC include)
target_compile_definitions(skcoop_runtime PRIVATE SKCOOP_RUNTIME_BUILD)
target_link_libraries(skcoop_runtime PRIVATE skcoop_windows_options skcoop_core)

add_library(skcoop_save_policy STATIC src/saves/save_policy.cpp)
target_include_directories(skcoop_save_policy PUBLIC include)
target_link_libraries(skcoop_save_policy PUBLIC skcoop_windows_options)

add_library(skcoop_save_settings STATIC src/saves/save_settings.cpp)
target_include_directories(skcoop_save_settings PUBLIC include)
target_link_libraries(skcoop_save_settings PUBLIC skcoop_save_policy)

add_library(skcoop_save_store STATIC src/saves/save_store.cpp)
target_include_directories(skcoop_save_store PUBLIC include)
target_link_libraries(skcoop_save_store PUBLIC skcoop_windows_options)

add_executable(skcoop_launcher src/launcher/main.cpp)
target_link_libraries(skcoop_launcher PRIVATE skcoop_launcher_support skcoop_save_settings)
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

  add_executable(skcoop_save_settings_tests tests/save_settings_tests.cpp)
  target_link_libraries(skcoop_save_settings_tests PRIVATE skcoop_save_settings)
  add_test(NAME save_settings_tests COMMAND skcoop_save_settings_tests)
  set_tests_properties(save_settings_tests PROPERTIES TIMEOUT 15)

  add_executable(skcoop_save_store_tests tests/save_store_tests.cpp)
  target_link_libraries(skcoop_save_store_tests PRIVATE skcoop_save_store)
  add_test(NAME save_storage_tests COMMAND skcoop_save_store_tests)
  set_tests_properties(save_storage_tests PROPERTIES TIMEOUT 45)
endif()
