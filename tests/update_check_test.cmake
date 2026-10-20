execute_process(COMMAND "${TOOL}" --check-update fixture.gsc
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 1 OR NOT error MATCHES "cannot be combined")
    message(FATAL_ERROR "Update check did not reject conversion inputs: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    HTTPS_PROXY=http://127.0.0.1:1 ALL_PROXY=http://127.0.0.1:1 NO_PROXY=
    "${TOOL}" --check-update
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT result EQUAL 1 OR NOT error MATCHES "Update check failed" OR output MATCHES "No newer release")
    message(FATAL_ERROR "Offline check did not report a failure: ${result} ${output} ${error}")
endif()
