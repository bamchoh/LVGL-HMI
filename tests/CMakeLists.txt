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
  test_check.cpp
  test_images.cpp
  test_animate.cpp
  test_driver_fins.cpp
  test_karakuri.cpp
  test_log_view.cpp
  test_meter.cpp
  test_datetime.cpp
  test_table.cpp
  test_xy_chart.cpp
  test_touch_filter.cpp
  test_alarm_stats.cpp
  test_schedule.cpp
  test_sound.cpp
  test_data_transfer.cpp
  test_template.cpp
  test_smtp.cpp
  test_smtp_socket.cpp
  test_mail.cpp
  test_net_upload.cpp
  test_upload.cpp
  test_driver_slmp.cpp
  test_driver_mqtt.cpp
  test_driver_modbus_serial.cpp
  test_driver_opcua.cpp
  test_driver_opcua_client.cpp
  test_screenshot.cpp
  test_demo_projects.cpp
  test_project.cpp
  test_ui.cpp
  test_ui_data_display.cpp
  test_ui_conditions.cpp
  test_system_settings.cpp
  test_splash.cpp
  test_script.cpp
  test_system_log.cpp
  test_logs.cpp
  test_trend.cpp
  test_recipe.cpp
  test_alarm_view.cpp
  test_key_service.cpp
  test_users.cpp
  test_texts.cpp
  test_screen_saver.cpp
  test_clock_tags.cpp
  test_popup.cpp
  test_project_archive.cpp
  test_transfer.cpp
  test_vnc.cpp
  ${HML_TEST_RUNTIME_SOURCES}
)
target_include_directories(hml_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${doctest_SOURCE_DIR})
target_compile_definitions(hml_tests PRIVATE HML_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(hml_tests PRIVATE lvgl hml_json hml_lua hml_sqlite hml_miniz hml_mbedtls)
if(HML_OPCUA)
  target_link_libraries(hml_tests PRIVATE open62541)
  target_compile_definitions(hml_tests PRIVATE HML_WITH_OPCUA=1)
endif()
if(WIN32)
  target_link_libraries(hml_tests PRIVATE ws2_32 setupapi winmm crypt32)
endif()
if(MSVC)
  target_compile_definitions(hml_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  # C5285: doctest 2.4.11 specializes std::tuple (fixed upstream after the 2.4.11 release)
  target_compile_options(hml_tests PRIVATE /W4 /EHsc /wd5285)
else()
  target_compile_options(hml_tests PRIVATE -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(hml_tests PRIVATE m Threads::Threads ${CMAKE_DL_LIBS})
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

# hml_runtime --check on the demo project: no window and no device on any backend; exit code 0 = no problems.
add_test(NAME hml_runtime_check COMMAND hml_runtime ${CMAKE_SOURCE_DIR}/projects/demo/project.json --check
  --render main,${CMAKE_BINARY_DIR}/check_main.png)
set_tests_properties(hml_runtime_check PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "no problems")

# Designer renderer (HTML/JS) unit tests: plain Node, no packages (node --test, Node 18+).
find_program(HML_NODE node)
if(HML_NODE)
  add_test(NAME designer_js COMMAND ${HML_NODE} --test ${CMAKE_SOURCE_DIR}/tools/designer/test/model.test.js)
else()
  message(STATUS "node not found: the designer JS tests are not registered with CTest")
endif()
