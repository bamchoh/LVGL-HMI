cmake_minimum_required(VERSION 3.24)
project(hml_runtime C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- options
#   HML_BACKEND
#     sdl   : SDL2 window - Windows / Linux desktop / macOS (default)
#     fbdev : Linux framebuffer (/dev/fb0) + evdev input - Raspberry Pi console, no X11 needed
#     drm   : Linux DRM/KMS (/dev/dri/card0) + evdev input - Raspberry Pi, needs libdrm-dev
set(HML_BACKEND "sdl" CACHE STRING "Display backend: sdl | fbdev | drm")
set_property(CACHE HML_BACKEND PROPERTY STRINGS sdl fbdev drm)
string(TOLOWER "${HML_BACKEND}" HML_BACKEND)
if(NOT HML_BACKEND MATCHES "^(sdl|fbdev|drm)$")
  message(FATAL_ERROR "HML_BACKEND must be sdl, fbdev or drm (got '${HML_BACKEND}')")
endif()
if(NOT WIN32 AND HML_BACKEND MATCHES "^(fbdev|drm)$" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "HML_BACKEND=${HML_BACKEND} is only available on Linux")
endif()

# On Windows SDL2 is built from source (static). On other systems the distro package is
# used (apt install libsdl2-dev); set HML_FETCH_SDL=ON to build it from source anyway.
if(WIN32)
  set(_hml_fetch_sdl_default ON)
else()
  set(_hml_fetch_sdl_default OFF)
endif()
option(HML_FETCH_SDL "Build SDL2 from source with FetchContent" ${_hml_fetch_sdl_default})
option(HML_OPCUA "Build the OPC UA server driver (open62541, MPL-2.0; needs Python 3 at build time)" ON)
if(WIN32)
  set(_hml_designer_default ON)
else()
  set(_hml_designer_default OFF)
endif()
option(HML_DESIGNER "Build the GUI project designer (hml_designer: webview + HTML/JS; Linux needs libwebkit2gtk-4.1-dev)" ${_hml_designer_default})
option(HML_TESTS "Build the unit tests (hml_tests, doctest; run with ctest)" ON)

if(MSVC)
  add_compile_options(/utf-8)
endif()

include(FetchContent)
set(_hml_fetch_targets "")

# ---------------------------------------------------------------- SDL2 (static, from source)
if(HML_BACKEND STREQUAL "sdl" AND HML_FETCH_SDL)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST   OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(sdl2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.8/SDL2-2.32.8.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  list(APPEND _hml_fetch_targets sdl2)
endif()

# ---------------------------------------------------------------- nlohmann/json (header only)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# ---------------------------------------------------------------- LVGL
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
# LVGL 9.4 option names: do not compile examples / demos / ThorVG (big build-time saving on a Pi)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
  URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.4.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# ---------------------------------------------------------------- Lua 5.4 (scripts, MIT)
# The release tarball has no CMake project: the sources are compiled below as hml_lua.
FetchContent_Declare(lua
  URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# ---------------------------------------------------------------- SQLite (system log, public domain)
# The amalgamation (one C file) is compiled below as hml_sqlite.
FetchContent_Declare(sqlite
  URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# ---------------------------------------------------------------- miniz (zip for project files, MIT)
# The amalgamation (one C file) is compiled below as hml_miniz.
FetchContent_Declare(miniz
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# ---------------------------------------------------------------- mbedTLS (TLS for MQTT, Apache-2.0 / GPL-2.0+)
# The 3.6 LTS release archive has its generated sources: the library is compiled directly
# (SOURCE_SUBDIR points nowhere so that its own CMake project is not added).
FetchContent_Declare(mbedtls
  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.4/mbedtls-3.6.4.tar.bz2
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR hml-no-cmake
)

# ---------------------------------------------------------------- open62541 (OPC UA, MPL-2.0)
# Built from the release source tarball with the reduced namespace zero (no git submodules
# needed). Needs Python 3 on the build host for the nodeset compiler.
if(HML_OPCUA)
  set(UA_NAMESPACE_ZERO "REDUCED" CACHE STRING "" FORCE)
  set(UA_MULTITHREADING 100 CACHE STRING "" FORCE)      # server API may be called from several threads
  set(UA_LOGLEVEL 300 CACHE STRING "" FORCE)
  # encryption (Sign / SignAndEncrypt, Basic256Sha256 ...) with the mbedTLS built below (hml_mbedtls):
  # its FindMbedTLS takes these cache entries as found
  set(UA_ENABLE_ENCRYPTION "MBEDTLS" CACHE STRING "" FORCE)
  set(UA_ENABLE_PUBSUB OFF CACHE BOOL "" FORCE)
  set(UA_ENABLE_PUBSUB_INFORMATIONMODEL OFF CACHE BOOL "" FORCE)
  set(UA_ENABLE_DISCOVERY OFF CACHE BOOL "" FORCE)
  set(UA_ENABLE_HISTORIZING OFF CACHE BOOL "" FORCE)
  set(UA_ENABLE_JSON_ENCODING OFF CACHE BOOL "" FORCE)
  set(UA_ENABLE_SUBSCRIPTIONS_EVENTS OFF CACHE BOOL "" FORCE)
  set(UA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(UA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(UA_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
  set(UA_MSVC_FORCE_STATIC_CRT OFF CACHE BOOL "" FORCE) # keep the same CRT (/MD) as the runtime
  set(UA_ENABLE_DEBUG_SANITIZER OFF CACHE BOOL "" FORCE)
  set(UA_ENABLE_HARDENING OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(open62541
    URL https://github.com/open62541/open62541/archive/refs/tags/v1.4.22.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
endif()

FetchContent_MakeAvailable(${_hml_fetch_targets} nlohmann_json lvgl lua sqlite)

# Lua core + the sandbox-safe standard libraries (no io / os / package / debug). Compiled as C++
# so that Lua errors are exceptions and unwind the runtime's C++ frames properly.
set(_hml_lua_sources "")
foreach(_f lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm
           lundump lvm lzio lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib)
  list(APPEND _hml_lua_sources ${lua_SOURCE_DIR}/src/${_f}.c)
endforeach()
set_source_files_properties(${_hml_lua_sources} PROPERTIES LANGUAGE CXX)
add_library(hml_lua STATIC ${_hml_lua_sources})
target_include_directories(hml_lua PUBLIC ${lua_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(hml_lua PRIVATE /W0)
else()
  target_compile_options(hml_lua PRIVATE -w)
endif()

FetchContent_MakeAvailable(mbedtls)
file(GLOB _hml_mbedtls_sources ${mbedtls_SOURCE_DIR}/library/*.c)
add_library(hml_mbedtls STATIC ${_hml_mbedtls_sources})
target_include_directories(hml_mbedtls PUBLIC $<BUILD_INTERFACE:${mbedtls_SOURCE_DIR}/include> PRIVATE ${mbedtls_SOURCE_DIR}/library)
if(MSVC)
  target_compile_options(hml_mbedtls PRIVATE /W0)
  target_compile_definitions(hml_mbedtls PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(hml_mbedtls PRIVATE -w)
endif()
if(WIN32)
  target_link_libraries(hml_mbedtls PUBLIC bcrypt ws2_32)     # entropy (BCryptGenRandom), sockets
endif()

FetchContent_MakeAvailable(miniz)
add_library(hml_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(hml_miniz PUBLIC ${miniz_SOURCE_DIR})
if(MSVC)
  target_compile_options(hml_miniz PRIVATE /W0)
  target_compile_definitions(hml_miniz PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(hml_miniz PRIVATE -w)
endif()

add_library(hml_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(hml_sqlite PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(hml_sqlite PRIVATE
  SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_OMIT_SHARED_CACHE
  SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_DQS=0 SQLITE_MAX_EXPR_DEPTH=0)
if(MSVC)
  target_compile_options(hml_sqlite PRIVATE /W0)
else()
  target_compile_options(hml_sqlite PRIVATE -w)
  find_package(Threads REQUIRED)
  target_link_libraries(hml_sqlite PUBLIC Threads::Threads)
endif()

if(HML_OPCUA)
  # open62541's CMake appends its own warning flags to CMAKE_C_FLAGS *in the cache*; configure it
  # after LVGL and restore the flags afterwards so that nothing else is affected.
  set(_hml_saved_c_flags "${CMAKE_C_FLAGS}")
  set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
  foreach(_l MBEDTLS_LIBRARY MBEDX509_LIBRARY MBEDCRYPTO_LIBRARY)
    set(${_l} hml_mbedtls CACHE STRING "" FORCE)
  endforeach()
  FetchContent_MakeAvailable(open62541)
  # open62541 links hml_mbedtls; its install and build-tree exports must hold it too (nothing is
  # installed from here, but CMake checks)
  install(TARGETS hml_mbedtls EXPORT open62541Targets ARCHIVE DESTINATION lib)
  export(TARGETS hml_mbedtls FILE ${CMAKE_BINARY_DIR}/hml_mbedtls_targets.cmake)
  set(CMAKE_C_FLAGS "${_hml_saved_c_flags}" CACHE STRING "Flags used by the C compiler during all build types." FORCE)
  set(CMAKE_C_FLAGS "${_hml_saved_c_flags}")
  if(MSVC)
    target_compile_options(open62541 PRIVATE /W0)
  else()
    target_compile_options(open62541 PRIVATE -w)
  endif()
endif()

add_library(hml_json INTERFACE)
target_include_directories(hml_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)

# ---------------------------------------------------------------- backend libraries
# The HML_BACKEND_* definition is PUBLIC on lvgl so that lv_conf.h (included by both LVGL
# and the runtime) enables the matching LVGL driver.
set(HML_BACKEND_LIBS "")
if(HML_BACKEND STREQUAL "sdl")
  target_compile_definitions(lvgl PUBLIC HML_BACKEND_SDL=1)
  if(HML_FETCH_SDL)
    set(HML_BACKEND_LIBS SDL2::SDL2-static)
  else()
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
      set(HML_BACKEND_LIBS SDL2::SDL2)
    else()
      set(HML_BACKEND_LIBS ${SDL2_LIBRARIES})
      target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    endif()
  endif()
elseif(HML_BACKEND STREQUAL "fbdev")
  target_compile_definitions(lvgl PUBLIC HML_BACKEND_FBDEV=1)
elseif(HML_BACKEND STREQUAL "drm")
  target_compile_definitions(lvgl PUBLIC HML_BACKEND_DRM=1)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
  set(HML_BACKEND_LIBS PkgConfig::DRM)
endif()
target_link_libraries(lvgl PUBLIC ${HML_BACKEND_LIBS})
if(MSVC)
  target_compile_options(lvgl PRIVATE /wd4244 /wd4267 /wd4305 /wd4996 /wd5287)
else()
  target_compile_options(lvgl PRIVATE -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- runtime (C++17)
file(GLOB HML_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(hml_runtime ${HML_SOURCES})
target_include_directories(hml_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hml_runtime PRIVATE lvgl hml_json hml_lua hml_sqlite hml_miniz hml_mbedtls)
if(HML_OPCUA)
  target_link_libraries(hml_runtime PRIVATE open62541)
  target_compile_definitions(hml_runtime PRIVATE HML_WITH_OPCUA=1)
endif()
if(WIN32)
  target_link_libraries(hml_runtime PRIVATE ws2_32 setupapi)
endif()
if(MSVC)
  target_compile_definitions(hml_runtime PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_compile_options(hml_runtime PRIVATE /W4 /EHsc)
else()
  target_compile_options(hml_runtime PRIVATE -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(hml_runtime PRIVATE m Threads::Threads)
endif()

install(TARGETS hml_runtime RUNTIME DESTINATION bin)

# ---------------------------------------------------------------- designer (optional)
if(HML_DESIGNER)
  add_subdirectory(tools/designer)
endif()

# ---------------------------------------------------------------- unit tests (optional)
if(HML_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "HML backend: ${HML_BACKEND}  OPC UA: ${HML_OPCUA}  designer: ${HML_DESIGNER}  tests: ${HML_TESTS}  (system: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR})")
