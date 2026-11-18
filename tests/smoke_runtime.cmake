# Smoke test for the real hml_runtime binary (SDL backend, headless via SDL's dummy video driver):
# run the demo project, take a screenshot, exit after a short time, and check the result.
#
#   cmake -DEXE=<hml_runtime> -DPROJECT=<project.json> -DOUT=<dir> -P smoke_runtime.cmake
if(NOT EXE OR NOT PROJECT OR NOT OUT)
  message(FATAL_ERROR "usage: cmake -DEXE=... -DPROJECT=... -DOUT=... -P smoke_runtime.cmake")
endif()
file(MAKE_DIRECTORY "${OUT}")
set(shot "${OUT}/smoke.bmp")
file(REMOVE "${shot}")

execute_process(
  COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          "${EXE}" "${PROJECT}" --exit-after 2500 --screenshot "${shot}" --reload-after 1200 --click 10,10,600
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  TIMEOUT 60
)
message(STATUS "hml_runtime exit code: ${rc}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "hml_runtime failed (${rc})\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endif()
if(NOT EXISTS "${shot}")
  message(FATAL_ERROR "no screenshot written to ${shot}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endif()
file(SIZE "${shot}" size)
if(size LESS 1000)
  message(FATAL_ERROR "screenshot is suspiciously small (${size} bytes)")
endif()
string(FIND "${out}" "screens built" built_pos)
if(built_pos EQUAL -1)
  message(FATAL_ERROR "runtime log does not report built screens\n--- stdout ---\n${out}")
endif()
string(FIND "${out}" "reloading screens" reload_pos)
if(reload_pos EQUAL -1)
  message(FATAL_ERROR "runtime log does not show the scripted reload\n--- stdout ---\n${out}")
endif()

# The demo project has "system_log" enabled: export the system log from a second process.
set(csv "${OUT}/smoke-log.csv")
file(REMOVE "${csv}")
execute_process(COMMAND "${EXE}" "${PROJECT}" --export-system-log "${csv}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
if(NOT rc EQUAL 0 OR NOT EXISTS "${csv}")
  message(FATAL_ERROR "--export-system-log failed (${rc})\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
endif()
file(READ "${csv}" log_text)
string(FIND "${log_text}" "screens built" log_pos)
if(log_pos EQUAL -1)
  message(FATAL_ERROR "exported log does not contain the runtime's messages\n${log_text}")
endif()

# ... and the data log (group "process" samples once a second), the alarm log and the operation log.
foreach(kind data alarm operation)
  set(out_csv "${OUT}/smoke-${kind}.csv")
  file(REMOVE "${out_csv}")
  set(extra "")
  if(kind STREQUAL "data")
    set(extra --group process)
  endif()
  execute_process(COMMAND "${EXE}" "${PROJECT}" --export-${kind}-log "${out_csv}" ${extra} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
  if(NOT rc EQUAL 0 OR NOT EXISTS "${out_csv}")
    message(FATAL_ERROR "--export-${kind}-log failed (${rc})
--- stdout ---
${out}
--- stderr ---
${err}")
  endif()
endforeach()
file(READ "${OUT}/smoke-data.csv" data_text)
string(FIND "${data_text}" "time,temp,pressure" data_pos)
if(data_pos EQUAL -1)
  message(FATAL_ERROR "exported data log has no header
${data_text}")
endif()
file(STRINGS "${OUT}/smoke-data.csv" data_lines)
list(LENGTH data_lines data_n)
if(data_n LESS 2)
  message(FATAL_ERROR "exported data log has no samples
${data_text}")
endif()
message(STATUS "smoke test passed: ${shot} (${size} bytes), log exported to ${csv}")
