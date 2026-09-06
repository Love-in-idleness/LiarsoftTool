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
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/root.xfl" "${TEST_ROOT}/root"
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
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/recursive_unpacked" "${TEST_ROOT}/root.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/recursive_unpacked/nested/scene/.meta.xml")
    message(FATAL_ERROR "Nested archives were not recursively unpacked: ${error}")
endif()

# Recursive unpack-only accepts an existing directory tree, visits every
# subdirectory, continues after a broken archive, and never packs the input.
file(MAKE_DIRECTORY "${TEST_ROOT}/unpack_tree/level/deep")
configure_file("${TEST_ROOT}/root.xfl"
               "${TEST_ROOT}/unpack_tree/level/deep/good.xfl" COPYONLY)
file(WRITE "${TEST_ROOT}/unpack_tree/level/broken.xfl" "not-an-archive")
execute_process(
    COMMAND "${TOOL}" -R --unpack-only "${TEST_ROOT}/unpack_tree"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR
   NOT EXISTS "${TEST_ROOT}/unpack_tree/level/deep/good/nested/scene/.meta.xml" OR
   EXISTS "${TEST_ROOT}/unpack_tree.xfl" OR
   NOT error MATCHES "Skipped archive.*broken.xfl")
    message(FATAL_ERROR "Recursive unpack-only directory traversal failed: ${error}")
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

# Recursive conversions: PNG -> WCG with LIM backup, OGG -> WAV, warnings for
# missing references, followed by recursive unpacking back to editable files.
file(MAKE_DIRECTORY "${TEST_ROOT}/convert")
configure_file("${SOURCE_DIR}/image.png" "${TEST_ROOT}/convert/image.png" COPYONLY)
file(WRITE "${TEST_ROOT}/convert/image.lim" "old-lim")
file(WRITE "${TEST_ROOT}/convert/orphan.txt" "#original\n>translation\n")
file(WRITE "${TEST_ROOT}/convert/orphan.ogg" "missing-template")
file(WRITE "${TEST_ROOT}/convert/broken.png" "not-an-image")
file(WRITE "${TEST_ROOT}/convert/broken.wcg" "stale-conversion")
file(WRITE "${TEST_ROOT}/convert/audio.wav"
    "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH")
string(ASCII 1 one)
file(WRITE "${TEST_ROOT}/convert/audio.ogg"
    "OggSAAAAAAAAAAAAAAAAAAAAAA${one}${one}Z")

execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/convert.xfl" "${TEST_ROOT}/convert"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Recursive conversion packing failed: ${error}")
endif()
if(NOT EXISTS "${TEST_ROOT}/convert/image.lim.old" OR
   EXISTS "${TEST_ROOT}/convert/image.lim" OR
   NOT EXISTS "${TEST_ROOT}/convert/image.wcg" OR
   NOT error MATCHES "same-name reference GSC not found" OR
   NOT error MATCHES "same-name WAV template not found" OR
   NOT error MATCHES "failed to load image")
    message(FATAL_ERROR "Recursive conversion or warnings are incorrect: ${error}")
endif()

execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/convert_unpacked" "${TEST_ROOT}/convert.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/convert_unpacked/image.png" OR
   NOT EXISTS "${TEST_ROOT}/convert_unpacked/audio.ogg" OR
   EXISTS "${TEST_ROOT}/convert_unpacked/broken.wcg")
    message(FATAL_ERROR "Recursive resource unpacking failed: ${error}")
endif()

# Raw metadata in a TSC is restored before recursive packing; the TSC itself is
# an editable source file and must not be stored in the archive.
file(MAKE_DIRECTORY "${TEST_ROOT}/tsc_restore")
file(WRITE "${TEST_ROOT}/tsc_restore/script.tsc"
    ";@gsc-raw-v1 size=3 fnv1a64=e71fa2190541574b\n"
    ";@gsc-raw 616263\n"
    ";@gsc-raw-end\n"
    "; ordinary comment\n")
file(WRITE "${TEST_ROOT}/tsc_restore/layout.xml" "resource")
execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/tsc_restore.xfl"
            "${TEST_ROOT}/tsc_restore"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Recursive TSC restoration failed: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/tsc_restore_unpacked"
            "${TEST_ROOT}/tsc_restore.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
file(READ "${TEST_ROOT}/tsc_restore_unpacked/script.gsc" restored HEX)
if(result OR NOT restored STREQUAL "616263" OR
   EXISTS "${TEST_ROOT}/tsc_restore_unpacked/script.tsc")
    message(FATAL_ERROR "Restored TSC was not packed as exact GSC: ${error}")
endif()

# Existing .lim.old protects the backup and skips image conversion. Any stale
# WCG target must not leak into the newly packed archive.
file(MAKE_DIRECTORY "${TEST_ROOT}/backup_collision")
configure_file("${SOURCE_DIR}/image.png"
               "${TEST_ROOT}/backup_collision/item.png" COPYONLY)
file(WRITE "${TEST_ROOT}/backup_collision/item.lim" "original-lim")
file(WRITE "${TEST_ROOT}/backup_collision/item.lim.old" "protected-backup")
file(WRITE "${TEST_ROOT}/backup_collision/item.wcg" "stale-conversion")
file(WRITE "${TEST_ROOT}/backup_collision/layout.xml" "resource")
execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/backup_collision.xfl"
            "${TEST_ROOT}/backup_collision"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT error MATCHES "backup already exists" OR
   NOT EXISTS "${TEST_ROOT}/backup_collision/item.lim" OR
   NOT EXISTS "${TEST_ROOT}/backup_collision/item.lim.old")
    message(FATAL_ERROR "Existing LIM backup was not protected: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/backup_collision_unpacked"
            "${TEST_ROOT}/backup_collision.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/backup_collision_unpacked/item.lim" OR
   NOT EXISTS "${TEST_ROOT}/backup_collision_unpacked/layout.xml" OR
   EXISTS "${TEST_ROOT}/backup_collision_unpacked/item.wcg")
    message(FATAL_ERROR "LIM backup collision filtering failed: ${error}")
endif()

# Without -R, editable files and child directories are left alone.
file(MAKE_DIRECTORY "${TEST_ROOT}/flat/child")
file(WRITE "${TEST_ROOT}/flat/layout.xml" "resource")
file(WRITE "${TEST_ROOT}/flat/source.png" "not-converted")
file(WRITE "${TEST_ROOT}/flat/child/data.gsc" "child")
execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/flat.xfl" "${TEST_ROOT}/flat"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR EXISTS "${TEST_ROOT}/flat/source.wcg" OR
   EXISTS "${TEST_ROOT}/flat/child.xfl")
    message(FATAL_ERROR "Non-recursive packing did recursive work: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" -o "${TEST_ROOT}/flat_unpacked" "${TEST_ROOT}/flat.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/flat_unpacked/layout.xml" OR
   EXISTS "${TEST_ROOT}/flat_unpacked/source.png")
    message(FATAL_ERROR "Non-recursive packing filter failed: ${error}")
endif()

# Operation filters: either direction works alone; enabling both matches no
# operation and must leave every requested output untouched.
execute_process(
    COMMAND "${TOOL}" --pack-only -o "${TEST_ROOT}/pack_only.xfl"
            "${TEST_ROOT}/flat"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/pack_only.xfl")
    message(FATAL_ERROR "Pack-only mode did not pack a directory: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" --unpack-only -o "${TEST_ROOT}/unpack_only"
            "${TEST_ROOT}/pack_only.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/unpack_only/layout.xml")
    message(FATAL_ERROR "Unpack-only mode did not unpack an archive: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" --unpack-only -o "${TEST_ROOT}/must_not_pack.xfl"
            "${TEST_ROOT}/flat"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR EXISTS "${TEST_ROOT}/must_not_pack.xfl")
    message(FATAL_ERROR "Unpack-only mode performed packing: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" --pack-only -o "${TEST_ROOT}/must_not_unpack"
            "${TEST_ROOT}/pack_only.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR EXISTS "${TEST_ROOT}/must_not_unpack")
    message(FATAL_ERROR "Pack-only mode performed unpacking: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" --pack-only --unpack-only
            -o "${TEST_ROOT}/both_enabled.xfl" "${TEST_ROOT}/flat"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR EXISTS "${TEST_ROOT}/both_enabled.xfl")
    message(FATAL_ERROR "Both operation filters enabled still did work: ${error}")
endif()
