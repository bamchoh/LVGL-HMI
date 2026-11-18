# HML Designer: native host (webview) + HTML/JS renderer. Built as part of the main project
# when HML_DESIGNER=ON (default on Windows). Needs the WebView2 runtime at run time on Windows
# (preinstalled on Windows 10/11) and libwebkit2gtk-4.1-dev at build time on Linux.
include(FetchContent)

set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_CHECKS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_PACKAGING OFF CACHE BOOL "" FORCE)
set(WEBVIEW_USE_COMPAT_MINGW OFF CACHE BOOL "" FORCE)
FetchContent_Declare(webview
  URL https://github.com/webview/webview/archive/refs/tags/0.12.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(webview)

# (project_archive.cpp: the project file format, shared with the runtime)
add_executable(hml_designer WIN32 host/main.cpp ${CMAKE_SOURCE_DIR}/src/project_archive.cpp)
target_include_directories(hml_designer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hml_designer PRIVATE webview::core hml_json hml_miniz)
target_compile_definitions(hml_designer PRIVATE
  HML_DESIGNER_VERSION="0.1"
  HML_DESIGNER_RENDERER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/renderer"
)
if(MSVC)
  target_compile_definitions(hml_designer PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
  target_compile_options(hml_designer PRIVATE /W4 /EHsc /wd4996)
  target_link_libraries(hml_designer PRIVATE comdlg32 shell32 ole32 ws2_32)
else()
  target_compile_options(hml_designer PRIVATE -Wall -Wextra -Wno-unused-parameter)
  find_package(Threads REQUIRED)
  target_link_libraries(hml_designer PRIVATE Threads::Threads)
endif()

# The page is loaded from <exe dir>/designer (falls back to the source tree while developing).
# Copied on every build, not only when the host is relinked: most designer changes touch the
# HTML / JS only.
add_custom_target(hml_designer_renderer ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/renderer $<TARGET_FILE_DIR:hml_designer>/designer
  COMMENT "Copying designer renderer files")
add_dependencies(hml_designer_renderer hml_designer)

install(TARGETS hml_designer RUNTIME DESTINATION bin)
install(DIRECTORY renderer/ DESTINATION bin/designer)
