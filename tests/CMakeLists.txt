# Unit tests (doctest, header only).
#
# hml_tests links the whole runtime except main.cpp and the platform backends; LVGL runs
# headless on an in-memory display (see lvgl_fixture.hpp), so the tests need no window,
# framebuffer or network and run in a few seconds.
FetchContent_Declare(doctest
  URL https://github.com/doctest/doctest/releases/download/v2.4.11/doctest.h
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(doctest)

file(GLOB HML_TEST_RUNTIME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER HML_TEST_RUNTIME_SOURCES EXCLUDE REGEX "/(main|platform_[a-z]+)\\.cpp$")

add_executable(hml_tests
  test_main.cpp
  lvgl_fixture.cpp
  test_util.cpp
  test_jsonutil.cpp
  test_tag.cpp
  test_driver_registry.cpp
  test_driver_sim.cpp
  test_driver_modbus.cpp
  test_driver_opcua.cpp
  test_screenshot.cpp
  test_demo_projects.cpp
  test_project.cpp
  test_ui.cpp
  ${HML_TEST_RUNTIME_SOURCES}
)
target_include_directories(hml_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${doctest_SOURCE_DIR})
target_compile_definitions(hml_tests PRIVATE HML_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(hml_tests PRIVATE lvgl hml_json)
if(HML_OPCUA)
  target_link_libraries(hml_tests PRIVATE open62541)
  target_compile_definitions(hml_tests PRIVATE HML_WITH_OPCUA=1)
endif()
if(WIN32)
  target_link_libraries(hml_tests PRIVATE ws2_32)
endif()
if(MSVC)
  target_compile_definitions(hml_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  # C5285: doctest 2.4.11 specializes std::tuple (fixed upstream after the 2.4.11 release)
  target_compile_options(hml_tests PRIVATE /W4 /EHsc /wd5285)
else()
  target_compile_options(hml_tests PRIVATE -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(hml_tests PRIVATE m Threads::Threads)
endif()

add_test(NAME hml_tests COMMAND hml_tests)

# Smoke test of the real runtime binary with the demo project. SDL's dummy video driver makes
# it headless, so it runs on CI too (the fbdev/drm backends need a real device and are skipped).
if(HML_BACKEND STREQUAL "sdl")
  add_test(NAME hml_runtime_smoke
    COMMAND ${CMAKE_COMMAND}
      -DEXE=$<TARGET_FILE:hml_runtime>
      -DPROJECT=${CMAKE_SOURCE_DIR}/projects/demo/project.json
      -DOUT=${CMAKE_BINARY_DIR}/smoke
      -P ${CMAKE_CURRENT_SOURCE_DIR}/smoke_runtime.cmake)
  set_tests_properties(hml_runtime_smoke PROPERTIES TIMEOUT 120)
endif()

# Designer renderer (HTML/JS) unit tests: plain Node, no packages (node --test, Node 18+).
find_program(HML_NODE node)
if(HML_NODE)
  add_test(NAME designer_js COMMAND ${HML_NODE} --test ${CMAKE_SOURCE_DIR}/tools/designer/test/model.test.js)
else()
  message(STATUS "node not found: the designer JS tests are not registered with CTest")
endif()
