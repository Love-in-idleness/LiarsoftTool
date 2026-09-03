cmake_minimum_required(VERSION 3.10)

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY
    "${TEST_ROOT}/root/good"
    "${TEST_ROOT}/root/bad"
    "${TEST_ROOT}/root/nested/scene"
    "${TEST_ROOT}/root/plain"
    "${TEST_ROOT}/root/empty_child"
    "${TEST_ROOT}/empty"
    "${TEST_ROOT}/empty_lwg")

set(meta "<Canvas><Width>1</Width><Height>1</Height><Items>")
file(WRITE "${TEST_ROOT}/root/good/.MeTa.XmL"
    "${meta}<Item x=\"0\" y=\"0\" flag=\"40\">image</Item>"
    "<Item x=\"0\" y=\"0\" flag=\"40\">mask</Item>"
    "<Item x=\"0\" y=\"0\" flag=\"40\">pngonly</Item></Items></Canvas>")
file(WRITE "${TEST_ROOT}/root/good/image.WCG" "WG-image")
file(WRITE "${TEST_ROOT}/root/good/mask.MsK" "BM-mask")
file(WRITE "${TEST_ROOT}/root/good/pngonly.PNG" "not-packed")

file(WRITE "${TEST_ROOT}/root/bad/.meta.xml" "not valid xml")
file(WRITE "${TEST_ROOT}/root/bad.lwg" "stale")
file(WRITE "${TEST_ROOT}/root/bad.xfl" "stale")
file(WRITE "${TEST_ROOT}/root/good.xfl" "obsolete")
file(WRITE "${TEST_ROOT}/root/nested/scene/.meta.xml"
    "${meta}<Item x=\"0\" y=\"0\" flag=\"40\">deep</Item></Items></Canvas>")
file(WRITE "${TEST_ROOT}/root/nested/scene/deep.wcg" "WG-deep")
file(WRITE "${TEST_ROOT}/root/plain/child.GSC" "child-script")
file(WRITE "${TEST_ROOT}/root/plain.lwg" "obsolete")
file(WRITE "${TEST_ROOT}/root/empty_child/project.cpp" "not-packed")
file(WRITE "${TEST_ROOT}/root/empty_child.lwg" "stale")
file(WRITE "${TEST_ROOT}/root/empty_child.xfl" "stale")

foreach(file IN ITEMS asset.LIM image.wcg script.GsC voice.WAV layout.Xml manual.LWG manual.XFL mask.msk)
    file(WRITE "${TEST_ROOT}/root/${file}" "allowed-${file}")
endforeach()
file(WRITE "${TEST_ROOT}/root/project.cpp" "not-packed")
file(WRITE "${TEST_ROOT}/root/source.PNG" "not-packed")

execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/root.xfl" "${TEST_ROOT}/root"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Packing test directory failed: ${error}")
endif()
if(NOT EXISTS "${TEST_ROOT}/root/nested/scene.lwg")
    message(FATAL_ERROR "Nested LWG directory was not packed")
endif()
if(NOT EXISTS "${TEST_ROOT}/root/nested.xfl" OR
   NOT EXISTS "${TEST_ROOT}/root/plain.xfl")
    message(FATAL_ERROR "XFL subdirectory was not packed")
endif()

execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/unpacked" "${TEST_ROOT}/root.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Extracting test XFL failed: ${error}")
endif()
foreach(file IN ITEMS asset.LIM image.wcg script.GsC voice.WAV layout.Xml manual.LWG manual.XFL mask.msk good.lwg nested.xfl plain.xfl)
    if(NOT EXISTS "${TEST_ROOT}/unpacked/${file}")
        message(FATAL_ERROR "Allowed file was not packed: ${file}")
    endif()
endforeach()
foreach(file IN ITEMS project.cpp source.PNG bad.lwg bad.xfl good.xfl plain.lwg empty_child.lwg empty_child.xfl)
    if(EXISTS "${TEST_ROOT}/unpacked/${file}")
        message(FATAL_ERROR "Excluded file was packed: ${file}")
    endif()
endforeach()

execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/plain_unpacked" "${TEST_ROOT}/root/plain.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/plain_unpacked/child.GSC")
    message(FATAL_ERROR "Generated child XFL is invalid: ${error}")
endif()

execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/nested_unpacked" "${TEST_ROOT}/root/nested.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/nested_unpacked/scene.lwg")
    message(FATAL_ERROR "Nested child archive was not included: ${error}")
endif()

execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/good_unpacked" "${TEST_ROOT}/root/good.lwg"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/good_unpacked/image.wcg" OR
   NOT EXISTS "${TEST_ROOT}/good_unpacked/mask.msk" OR
   EXISTS "${TEST_ROOT}/good_unpacked/pngonly")
    message(FATAL_ERROR "LWG extension filtering failed: ${error}")
endif()

file(WRITE "${TEST_ROOT}/empty/project.cpp" "not-packed")
execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/empty.xfl" "${TEST_ROOT}/empty"
    RESULT_VARIABLE result)
if(NOT result)
    message(FATAL_ERROR "Empty XFL directory unexpectedly succeeded")
endif()

file(WRITE "${TEST_ROOT}/empty_lwg/.meta.xml" "${meta}</Items></Canvas>")
execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/empty.lwg" "${TEST_ROOT}/empty_lwg"
    RESULT_VARIABLE result)
if(NOT result)
    message(FATAL_ERROR "Empty LWG directory unexpectedly succeeded")
endif()
