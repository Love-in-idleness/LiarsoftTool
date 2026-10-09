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
    COMMAND "${TOOL}" -R -j 1 -o "${TEST_ROOT}/root.xfl" "${TEST_ROOT}/root"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Packing test directory failed: ${error}")
endif()
file(SHA256 "${TEST_ROOT}/root.xfl" serial_hash)
set(serial_warnings "${error}")
execute_process(COMMAND "${TOOL}" -R --jobs 8 -o "${TEST_ROOT}/root.xfl" "${TEST_ROOT}/root"
    RESULT_VARIABLE result ERROR_VARIABLE error)
file(SHA256 "${TEST_ROOT}/root.xfl" parallel_hash)
if(result OR NOT serial_hash STREQUAL parallel_hash OR NOT error STREQUAL serial_warnings)
    message(FATAL_ERROR "CLI worker selection changed packed bytes or warning order: ${error}")
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

# Recursive conversions: same-name PNG -> WCG and WebP -> LIM, OGG -> WAV, warnings for
# missing references, followed by recursive unpacking back to editable files.
file(MAKE_DIRECTORY "${TEST_ROOT}/convert")
execute_process(COMMAND "${IMAGE_TEST}" "${TEST_ROOT}/convert"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Creating valid image fixtures failed: ${error}")
endif()
file(RENAME "${TEST_ROOT}/convert/image.webp" "${TEST_ROOT}/convert/image.WEBP")
file(WRITE "${TEST_ROOT}/convert/image.lim.old" "existing-backup")
file(WRITE "${TEST_ROOT}/convert/orphan.txt" "#original\n>translation\n")
file(WRITE "${TEST_ROOT}/convert/orphan.ogg" "invalid-audio")
file(WRITE "${TEST_ROOT}/convert/broken.png" "not-an-image")
file(WRITE "${TEST_ROOT}/convert/broken.wcg" "stale-conversion")
file(WRITE "${TEST_ROOT}/convert/broken.webp" "not-an-image")
file(WRITE "${TEST_ROOT}/convert/broken.lim" "stale-conversion")
execute_process(COMMAND "${AUDIO_TEST}" "${TEST_ROOT}/convert"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Creating valid audio fixtures failed: ${error}")
endif()
file(READ "${TEST_ROOT}/convert/badref.wav" original_pcm HEX)

# Default PCM and opt-in Vorbis wrapping both work without an original WAV.
execute_process(
    COMMAND "${TOOL}" --pack-only -r "${TEST_ROOT}/nonexistent-reference.wav"
            -o "${TEST_ROOT}/decoded.wav" "${TEST_ROOT}/convert/audio.ogg"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/decoded.wav")
    message(FATAL_ERROR "Default PCM conversion without template failed: ${error}")
endif()
file(READ "${TEST_ROOT}/decoded.wav" decoded_pcm HEX)
if(NOT decoded_pcm STREQUAL original_pcm)
    message(FATAL_ERROR "Default CLI audio output was not decoded PCM")
endif()
execute_process(
    COMMAND "${TOOL}" --vorbis-in-wav --pack-only
            -o "${TEST_ROOT}/enabled.wav"
            "${TEST_ROOT}/convert/audio.ogg"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/enabled.wav")
    message(FATAL_ERROR "Template-free compressed audio wrapping failed: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" --unpack-only -o "${TEST_ROOT}/enabled.ogg" "${TEST_ROOT}/enabled.wav"
    RESULT_VARIABLE result ERROR_VARIABLE error)
file(READ "${TEST_ROOT}/convert/audio.ogg" original_ogg HEX)
file(READ "${TEST_ROOT}/enabled.ogg" roundtrip_ogg HEX)
if(result OR NOT original_ogg STREQUAL roundtrip_ogg)
    message(FATAL_ERROR "Compressed audio round trip changed the Ogg stream: ${error}")
endif()

# Both modes propagate through nested directories. No same-name WAV exists.
file(MAKE_DIRECTORY "${TEST_ROOT}/audio_nested/sub")
file(WRITE "${TEST_ROOT}/audio_nested/sub/layout.xml" "resource")
configure_file("${TEST_ROOT}/convert/audio.ogg"
               "${TEST_ROOT}/audio_nested/sub/voice.OGG" COPYONLY)
execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/audio_pcm.xfl" "${TEST_ROOT}/audio_nested"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Recursive PCM conversion failed: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" -R --unpack-only -o "${TEST_ROOT}/audio_pcm_unpacked"
            "${TEST_ROOT}/audio_pcm.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
file(READ "${TEST_ROOT}/audio_pcm_unpacked/sub/voice.wav" nested_pcm HEX)
if(result OR NOT nested_pcm STREQUAL original_pcm OR
   EXISTS "${TEST_ROOT}/audio_pcm_unpacked/sub/voice.ogg")
    message(FATAL_ERROR "Recursive PCM mode did not reach the child")
endif()
execute_process(
    COMMAND "${TOOL}" -R --vorbis-in-wav
            -o "${TEST_ROOT}/audio_wrapped.xfl" "${TEST_ROOT}/audio_nested"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Recursive Vorbis mode failed: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" -R --unpack-only -o "${TEST_ROOT}/audio_wrapped_unpacked"
            "${TEST_ROOT}/audio_wrapped.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
file(READ "${TEST_ROOT}/audio_wrapped_unpacked/sub/voice.ogg" nested_ogg HEX)
if(result OR NOT nested_ogg STREQUAL original_ogg)
    message(FATAL_ERROR "Recursive Vorbis mode did not reach the child")
endif()

# Bad audio must warn/continue and exclude an old WAV, in both modes.
file(WRITE "${TEST_ROOT}/audio_nested/sub/voice.OGG" "broken-ogg")
foreach(mode "" "--vorbis-in-wav")
    execute_process(COMMAND "${TOOL}" -R ${mode} -o "${TEST_ROOT}/audio_failed.xfl"
                    "${TEST_ROOT}/audio_nested" RESULT_VARIABLE result ERROR_VARIABLE error)
    if(result OR NOT error MATCHES "Ogg")
        message(FATAL_ERROR "Invalid nested Ogg did not warn and continue: ${error}")
    endif()
    execute_process(COMMAND "${TOOL}" -R --unpack-only -o "${TEST_ROOT}/audio_failed_unpacked"
                    "${TEST_ROOT}/audio_failed.xfl" RESULT_VARIABLE result ERROR_VARIABLE error)
    if(result OR EXISTS "${TEST_ROOT}/audio_failed_unpacked/sub/voice.wav" OR
       NOT EXISTS "${TEST_ROOT}/audio_failed_unpacked/sub/layout.xml")
        message(FATAL_ERROR "Invalid nested Ogg retained its stale WAV in the archive")
    endif()
endforeach()

execute_process(
    COMMAND "${TOOL}" -R --vorbis-in-wav
            -o "${TEST_ROOT}/convert.xfl" "${TEST_ROOT}/convert"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Recursive conversion packing failed: ${error}")
endif()
if(NOT EXISTS "${TEST_ROOT}/convert/image.lim" OR
   NOT EXISTS "${TEST_ROOT}/convert/image.wcg" OR
   NOT error MATCHES "same-name reference GSC not found" OR
   NOT error MATCHES "Ogg" OR
   NOT error MATCHES "failed to load image" OR
   NOT error MATCHES "Not a valid WebP")
    message(FATAL_ERROR "Recursive conversion or warnings are incorrect: ${error}")
endif()
file(READ "${TEST_ROOT}/convert/image.lim.old" retained_backup)
if(NOT retained_backup STREQUAL "existing-backup")
    message(FATAL_ERROR "Unrelated LIM backup was changed")
endif()
file(READ "${TEST_ROOT}/convert/badref.wav" retained_pcm HEX)
if(NOT retained_pcm STREQUAL original_pcm)
    message(FATAL_ERROR "Failed Ogg conversion overwrote the existing PCM WAV")
endif()

execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/convert_unpacked" "${TEST_ROOT}/convert.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/convert_unpacked/image.png" OR
   NOT EXISTS "${TEST_ROOT}/convert_unpacked/image.webp" OR
   NOT EXISTS "${TEST_ROOT}/convert_unpacked/audio.ogg" OR
   NOT EXISTS "${TEST_ROOT}/convert_unpacked/pcm.wav" OR
   EXISTS "${TEST_ROOT}/convert_unpacked/pcm.ogg" OR
   EXISTS "${TEST_ROOT}/convert_unpacked/badref.wav" OR
   EXISTS "${TEST_ROOT}/convert_unpacked/broken.wcg" OR
   EXISTS "${TEST_ROOT}/convert_unpacked/broken.lim" OR
   EXISTS "${TEST_ROOT}/convert_unpacked/image.lim.old")
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

# CLI defaults and direction switches must use WebP/LIM without touching WCG/PNG.
file(MAKE_DIRECTORY "${TEST_ROOT}/webp_cli")
configure_file("${TEST_ROOT}/convert/image.lim"
               "${TEST_ROOT}/webp_cli/item.LIM" COPYONLY)
execute_process(
    COMMAND "${TOOL}" --unpack-only "${TEST_ROOT}/webp_cli/item.LIM"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/webp_cli/item.webp" OR
   EXISTS "${TEST_ROOT}/webp_cli/item.png")
    message(FATAL_ERROR "CLI LIM output is not WebP: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" --pack-only "${TEST_ROOT}/webp_cli/item.webp"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/webp_cli/item.lim" OR
   EXISTS "${TEST_ROOT}/webp_cli/item.wcg")
    message(FATAL_ERROR "CLI WebP output is not LIM: ${error}")
endif()
file(READ "${TEST_ROOT}/webp_cli/item.LIM" expected_lim HEX)
file(READ "${TEST_ROOT}/webp_cli/item.lim" rebuilt_lim HEX)
if(NOT rebuilt_lim STREQUAL expected_lim)
    message(FATAL_ERROR "CLI LIM/WebP/LIM round trip changed the canonical image")
endif()

# LWG metadata references LIM after recursive WebP conversion, not PNG/WCG.
file(MAKE_DIRECTORY "${TEST_ROOT}/webp_lwg")
configure_file("${TEST_ROOT}/convert/image.WEBP"
               "${TEST_ROOT}/webp_lwg/background.webp" COPYONLY)
file(WRITE "${TEST_ROOT}/webp_lwg/.meta.xml"
    "${meta}<Item x=\"0\" y=\"0\" flag=\"40\">background</Item></Items></Canvas>")
execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/webp.lwg" "${TEST_ROOT}/webp_lwg"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/webp_lwg/background.lim" OR
   EXISTS "${TEST_ROOT}/webp_lwg/background.wcg")
    message(FATAL_ERROR "Recursive WebP packing into LWG failed: ${error}")
endif()
execute_process(
    COMMAND "${TOOL}" -R -o "${TEST_ROOT}/webp_lwg_unpacked" "${TEST_ROOT}/webp.lwg"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result OR NOT EXISTS "${TEST_ROOT}/webp_lwg_unpacked/background.webp" OR
   EXISTS "${TEST_ROOT}/webp_lwg_unpacked/background.png")
    message(FATAL_ERROR "Recursive LIM extraction from LWG failed: ${error}")
endif()
file(READ "${TEST_ROOT}/webp_lwg_unpacked/background.lim" extracted_lim HEX)
if(NOT extracted_lim STREQUAL expected_lim)
    message(FATAL_ERROR "LWG WebP conversion changed the LIM payload")
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

# Use an archive dated two days ago, not freshly written fixtures whose times
# could accidentally agree even without timestamp inheritance.
execute_process(COMMAND "${GUI_TEST}" "${TEST_ROOT}/timestamps"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Creating dated archive failed: ${error}")
endif()
function(expect_source_time source output)
    file(TIMESTAMP "${source}" source_time "%s" UTC)
    file(TIMESTAMP "${output}" output_time "%s" UTC)
    if(source_time STREQUAL "" OR NOT output_time STREQUAL source_time)
        message(FATAL_ERROR "Modification time not inherited: ${source} -> ${output}")
    endif()
endfunction()
execute_process(COMMAND "${TOOL}" -R --gsc-to-tsc
    -o "${TEST_ROOT}/dated_unpacked" "${TEST_ROOT}/timestamps/dated.xfl"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Recursive dated archive extraction failed: ${error}")
endif()
foreach(name IN ITEMS nested.xfl nested/layout.xml scene.lwg scene/.meta.xml
        scene/background.lim scene/background.webp image.wcg image.png image.lim
        image.webp voice.wav voice.ogg script.gsc script.tsc)
    expect_source_time("${TEST_ROOT}/timestamps/dated.xfl" "${TEST_ROOT}/dated_unpacked/${name}")
endforeach()
foreach(pair IN ITEMS "wcg;png" "lim;webp" "wav;ogg" "gsc;txt")
    list(GET pair 0 source_ext)
    list(GET pair 1 output_ext)
    if(source_ext STREQUAL "wav")
        set(stem voice)
    elseif(source_ext STREQUAL "gsc")
        set(stem script)
    else()
        set(stem image)
    endif()
    set(source "${TEST_ROOT}/dated_unpacked/${stem}.${source_ext}")
    set(output "${TEST_ROOT}/timestamps/direct.${output_ext}")
    execute_process(COMMAND "${TOOL}" -o "${output}" "${source}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(result)
        message(FATAL_ERROR "Direct ${source_ext} extraction failed: ${error}")
    endif()
    expect_source_time("${source}" "${output}")
endforeach()
execute_process(COMMAND "${TOOL}" -o "${TEST_ROOT}/dated_scene"
    "${TEST_ROOT}/dated_unpacked/scene.lwg" RESULT_VARIABLE result ERROR_VARIABLE error)
if(result)
    message(FATAL_ERROR "Direct dated LWG extraction failed: ${error}")
endif()
foreach(name IN ITEMS .meta.xml background.lim)
    expect_source_time("${TEST_ROOT}/dated_unpacked/scene.lwg" "${TEST_ROOT}/dated_scene/${name}")
endforeach()
# Reject invalid thread counts before touching any input/output.
foreach(jobs "-1" "65" "abc" "2x" "99999999999999999999999999" "")
    execute_process(COMMAND "${TOOL}" --jobs "${jobs}" --help
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result OR NOT error MATCHES "jobs")
        message(FATAL_ERROR "Invalid thread count was not rejected: '${jobs}' ${error}")
    endif()
endforeach()
execute_process(COMMAND "${TOOL}" -j RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result OR NOT error MATCHES "requires a value")
    message(FATAL_ERROR "Missing worker argument was not rejected")
endif()
