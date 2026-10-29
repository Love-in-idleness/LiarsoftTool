execute_process(COMMAND "${XGETTEXT}" --language=C++ --from-code=UTF-8
    --keyword=_ --keyword=B_ --keyword=E_ --keyword=N_ -o "${BUILD_DIR}/gui-test.pot" src/gui.cpp src/gui_browser.cpp src/gui_exe_editor.cpp
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot collect GUI messages: ${error}")
endif()
foreach(language ru zh_CN ja)
    execute_process(COMMAND "${MSGCMP}" "${SOURCE_DIR}/po/${language}.po" "${BUILD_DIR}/gui-test.pot"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Incomplete ${language} GUI translations: ${error}")
    endif()
endforeach()
