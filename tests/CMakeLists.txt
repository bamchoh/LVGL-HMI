# Unit tests for the LVGL-independent parts of the runtime (doctest, header only).
# Only the sources under test are compiled in, so the tests build and run in seconds
# and do not need SDL / a display.
FetchContent_Declare(doctest
  URL https://github.com/doctest/doctest/releases/download/v2.4.11/doctest.h
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(doctest)

set(HML_TEST_SOURCES
  ${CMAKE_SOURCE_DIR}/src/util.cpp
  ${CMAKE_SOURCE_DIR}/src/jsonutil.cpp
  ${CMAKE_SOURCE_DIR}/src/tag.cpp
)

add_executable(hml_tests
  test_main.cpp
  test_jsonutil.cpp
  test_tag.cpp
  ${HML_TEST_SOURCES}
)
target_include_directories(hml_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${doctest_SOURCE_DIR})
target_link_libraries(hml_tests PRIVATE hml_json)
if(MSVC)
  target_compile_definitions(hml_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  # C5285: doctest 2.4.11 specializes std::tuple (fixed upstream after the 2.4.11 release)
  target_compile_options(hml_tests PRIVATE /W4 /EHsc /wd5285)
else()
  target_compile_options(hml_tests PRIVATE -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(hml_tests PRIVATE Threads::Threads)
endif()

add_test(NAME hml_tests COMMAND hml_tests)
