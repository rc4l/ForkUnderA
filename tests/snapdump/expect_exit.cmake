# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] Runs snap_dump and compares its exit status with the one wanted. CTest's WILL_FAIL
# only says a test must fail, not which status it must fail with, and the status is the contract
# here: 0 read whole, 1 the bytes could not be got, 2 the bytes are wrong. A tool that answered 1
# where it should answer 2 would pass WILL_FAIL and still mislead every script reading it.
#
#   cmake -D TOOL=<snap_dump> -D IN=<recording> -D WANT=<status> -P expect_exit.cmake

if(NOT DEFINED TOOL OR NOT DEFINED IN OR NOT DEFINED WANT)
  message(FATAL_ERROR "expect_exit.cmake needs TOOL, IN and WANT")
endif()

execute_process(
  COMMAND "${TOOL}" "${IN}"
  RESULT_VARIABLE got
  ERROR_VARIABLE err
  OUTPUT_QUIET)

if(NOT got STREQUAL WANT)
  message(FATAL_ERROR
    "snap_dump \"${IN}\": exit ${got}, wanted ${WANT}\nits stderr was: ${err}")
endif()
