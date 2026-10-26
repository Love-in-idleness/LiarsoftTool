cmake_minimum_required(VERSION 3.10)
project(LiarsoftTool VERSION 2.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(BUILD_RUNTIME "Build the experimental RScript runtime and its tests" OFF)
find_package(Threads REQUIRED)
find_package(CURL 7.85 REQUIRED)

# Keep detection inside both executables, without another runtime DLL/SO.
find_path(UCHARDET_INCLUDE_DIR uchardet/uchardet.h)
find_library(UCHARDET_LIBRARY NAMES "${CMAKE_STATIC_LIBRARY_PREFIX}uchardet${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(NOT UCHARDET_INCLUDE_DIR OR NOT UCHARDET_LIBRARY)
    message(FATAL_ERROR "uchardet development files, including its static library, are required")
endif()
add_library(Uchardet::uchardet STATIC IMPORTED)
set_target_properties(Uchardet::uchardet PROPERTIES
    IMPORTED_LOCATION "${UCHARDET_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${UCHARDET_INCLUDE_DIR}")

# libvorbisfile provides the same PCM decoder on Linux and Windows.
find_path(VORBIS_INCLUDE_DIR vorbis/vorbisfile.h)
find_path(OGG_INCLUDE_DIR ogg/ogg.h)
find_library(VORBISFILE_LIBRARY NAMES vorbisfile)
find_library(VORBIS_LIBRARY NAMES vorbis)
find_library(OGG_LIBRARY NAMES ogg)
if(NOT VORBIS_INCLUDE_DIR OR NOT OGG_INCLUDE_DIR OR NOT VORBISFILE_LIBRARY
   OR NOT VORBIS_LIBRARY OR NOT OGG_LIBRARY)
    message(FATAL_ERROR "libvorbisfile/libvorbis/libogg development files are required")
endif()
add_library(Vorbis::file UNKNOWN IMPORTED)
set_target_properties(Vorbis::file PROPERTIES
    IMPORTED_LOCATION "${VORBISFILE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${VORBIS_INCLUDE_DIR};${OGG_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${VORBIS_LIBRARY};${OGG_LIBRARY}")

find_path(WEBP_INCLUDE_DIR webp/encode.h)
find_library(WEBP_LIBRARY NAMES webp)
if(NOT WEBP_INCLUDE_DIR OR NOT WEBP_LIBRARY)
    message(FATAL_ERROR "libwebp development files are required")
endif()
add_library(WebP::webp UNKNOWN IMPORTED)
set_target_properties(WebP::webp PROPERTIES
    IMPORTED_LOCATION "${WEBP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WEBP_INCLUDE_DIR}")
# Newer static libwebp builds also require libsharpyuv.
get_filename_component(WEBP_LIBRARY_DIR "${WEBP_LIBRARY}" DIRECTORY)
find_library(WEBP_SHARPYUV_LIBRARY NAMES sharpyuv HINTS "${WEBP_LIBRARY_DIR}")
if(WEBP_SHARPYUV_LIBRARY)
    set_property(TARGET WebP::webp PROPERTY
        INTERFACE_LINK_LIBRARIES "${WEBP_SHARPYUV_LIBRARY}")
endif()

# Find iconv (needed on Unix; Windows uses Win32 API via encoding.cpp)
if(NOT WIN32)
    find_package(Iconv REQUIRED)
endif()

# Both desktop platforms use the GTK4/libadwaita C API from C++.
option(BUILD_GUI "Build the GTK4/libadwaita GUI" ON)
if(BUILD_GUI)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1>=1.5 gtk4>=4.10)
    find_package(Intl REQUIRED)
    find_program(MSGFMT msgfmt REQUIRED)
    foreach(language ru zh_CN ja)
        set(catalog "${CMAKE_CURRENT_BINARY_DIR}/locale/${language}/LC_MESSAGES/liarsofttool.mo")
        add_custom_command(OUTPUT "${catalog}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/${language}/LC_MESSAGES"
            COMMAND ${MSGFMT} --check -o "${catalog}" "${CMAKE_CURRENT_SOURCE_DIR}/po/${language}.po"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/${language}.po")
        list(APPEND GUI_CATALOGS "${catalog}")
        install(FILES "${catalog}" DESTINATION "share/locale/${language}/LC_MESSAGES")
    endforeach()
    add_custom_target(gui_translations ALL DEPENDS ${GUI_CATALOGS})
    add_library(liarsofttool_gui_preferences STATIC src/gui_preferences.cpp)
    target_link_libraries(liarsofttool_gui_preferences PUBLIC PkgConfig::ADWAITA Intl::Intl liarsofttool_core)
    if(NOT WIN32)
        set(embedded_catalogs "${CMAKE_CURRENT_BINARY_DIR}/gui_catalogs.h")
        add_custom_command(OUTPUT "${embedded_catalogs}"
            COMMAND ${CMAKE_COMMAND}
                "-DCATALOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/locale"
                "-DOUTPUT=${embedded_catalogs}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_gui_catalogs.cmake"
            DEPENDS ${GUI_CATALOGS} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_gui_catalogs.cmake")
        target_sources(liarsofttool_gui_preferences PRIVATE "${embedded_catalogs}")
        target_include_directories(liarsofttool_gui_preferences PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(liarsofttool_gui_preferences PRIVATE LIARSOFT_EMBEDDED_CATALOGS)
    endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(liarsofttool_updates STATIC src/update_check.cpp)
target_compile_definitions(liarsofttool_updates PUBLIC LIARSOFTTOOL_VERSION="${PROJECT_VERSION}")
target_link_libraries(liarsofttool_updates PUBLIC CURL::libcurl)

# Windows resource file (manifest for visual styles + DPI awareness)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Find windres (prefixed with cross-compiler triplet, e.g. x86_64-w64-mingw32-windres)
    get_filename_component(_MINGW_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    get_filename_component(_MINGW_CXX ${CMAKE_CXX_COMPILER} NAME)
    string(REGEX REPLACE "-g\\+\\+(-posix|-win32)?$" "" _MINGW_PFX "${_MINGW_CXX}")
    find_program(WINDRES NAMES ${_MINGW_PFX}-windres windres
        HINTS ${_MINGW_DIR}
        REQUIRED)
    message(STATUS "Windows resource compiler: ${WINDRES}")
    set(RESOURCE_OBJ ${CMAKE_CURRENT_BINARY_DIR}/app.res.o)
    add_custom_command(
        OUTPUT ${RESOURCE_OBJ}
        COMMAND ${WINDRES} -O coff -I${CMAKE_CURRENT_SOURCE_DIR} -i ${CMAKE_CURRENT_SOURCE_DIR}/app.rc -o ${RESOURCE_OBJ}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app.rc ${CMAKE_CURRENT_SOURCE_DIR}/app.manifest
        COMMENT "Compiling Windows resources (app.rc -> app.res.o)"
    )
endif()

# Compile the conversion core once for the CLI, GUI and test executables.
add_library(liarsofttool_core STATIC
    src/archive_index.cpp
    src/resource_preview.cpp
    src/conversion.cpp
    src/bmp_normalizer.cpp
    src/bigendian.cpp
    src/encoding.cpp
    src/fileio.cpp
    src/gscfile.cpp
    src/gsc_decompiler.cpp
    src/transfile.cpp
    src/xflarchive.cpp
    src/cg_decompress.cpp
    src/wcg_decoder.cpp
    src/lim_decoder.cpp
    src/lwg_decoder.cpp
    src/wav_ogg.cpp
    # src/exe_patch.cpp # EXE conversion is disabled in production builds.
    src/stb_impl.cpp
    src/stb_image_impl.cpp
)
target_link_libraries(liarsofttool_core PUBLIC WebP::webp Vorbis::file Threads::Threads Uchardet::uchardet)
if(NOT WIN32)
    target_link_libraries(liarsofttool_core PUBLIC Iconv::Iconv)
endif()

# ---- CLI target ----
add_executable(liarsofttool src/cli_main.cpp)
target_link_libraries(liarsofttool PRIVATE liarsofttool_updates)
if(WIN32)
    target_sources(liarsofttool PRIVATE ${RESOURCE_OBJ})
    target_link_libraries(liarsofttool PRIVATE shell32)
endif()

# ---- GUI target ----
if(BUILD_GUI)
    add_executable(liarsofttool-gui
        src/gui_main.cpp
        src/gui.cpp
        src/gui_browser.cpp
        src/gui_common.cpp
    )
    target_link_libraries(liarsofttool-gui PRIVATE liarsofttool_gui_preferences liarsofttool_updates)
    add_dependencies(liarsofttool-gui gui_translations)
    if(WIN32)
        set_target_properties(liarsofttool-gui PROPERTIES WIN32_EXECUTABLE TRUE)
        target_sources(liarsofttool-gui PRIVATE ${RESOURCE_OBJ})
    endif()
    message(STATUS "GTK GUI: enabled (liarsofttool-gui)")
else()
    message(STATUS "GUI: disabled (BUILD_GUI=OFF)")
endif()

# Install
install(TARGETS liarsofttool DESTINATION bin)
install(FILES docs/THIRD_PARTY_AUDIO_LICENSES.md docs/THIRD_PARTY_UCHARDET_LICENSES.md
    DESTINATION share/doc/LiarsoftTool)
if(BUILD_GUI)
    install(TARGETS liarsofttool-gui DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(update_check_test tests/update_check_test.cpp)
    target_link_libraries(update_check_test PRIVATE liarsofttool_updates)
    add_test(NAME update_check COMMAND update_check_test)
    add_test(NAME update_check_cli COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:liarsofttool>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/update_check_test.cmake)
    find_library(VORBISENC_LIBRARY NAMES vorbisenc)
    if(NOT VORBISENC_LIBRARY)
        message(FATAL_ERROR "libvorbisenc is required to generate audio test inputs")
    endif()
    add_executable(encoding_test tests/encoding_test.cpp)
    add_test(NAME encoding COMMAND encoding_test)

    add_executable(bmp_test tests/bmp_test.cpp)
    add_test(NAME bmp COMMAND ${CMAKE_COMMAND}
        -DBMP_TEST=$<TARGET_FILE:bmp_test> -DTOOL=$<TARGET_FILE:liarsofttool>
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/bmp_test_data
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bmp_test.cmake)

    add_executable(gscfile_test tests/gscfile_test.cpp)
    add_test(NAME gscfile COMMAND gscfile_test)
    add_executable(gsc_encoding_test tests/gsc_encoding_test.cpp)
    target_link_libraries(gsc_encoding_test PRIVATE liarsofttool_core)
    add_test(NAME gsc_encoding COMMAND gsc_encoding_test)
    add_test(NAME gsc_encoding_cli COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:liarsofttool> -DFIXTURES=$<TARGET_FILE:gsc_encoding_test>
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/gsc_encoding_data
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gsc_encoding_test.cmake)

    add_executable(fileio_test tests/fileio_test.cpp)
    add_test(NAME fileio COMMAND fileio_test)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_library(fileio_faults SHARED tests/fileio_faults.cpp)
        target_link_libraries(fileio_faults PRIVATE ${CMAKE_DL_LIBS})
        set_tests_properties(fileio PROPERTIES
            ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:fileio_faults>")
    endif()
    add_executable(wav_ogg_test tests/wav_ogg_test.cpp)
    add_test(NAME wav_ogg COMMAND wav_ogg_test)

    add_executable(exe_patch_test tests/exe_patch_test.cpp src/exe_patch.cpp)
    add_test(NAME exe_patch COMMAND exe_patch_test)
    add_test(NAME exe_conversion_disabled COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:liarsofttool> -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/exe_disabled_data
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/exe_disabled_test.cmake)

    add_executable(gsc_decompiler_test tests/gsc_decompiler_test.cpp)
    add_test(NAME gsc_decompiler COMMAND gsc_decompiler_test)
    add_executable(tsc_check_test tests/tsc_check_test.cpp)
    add_executable(tsc_document_test tests/tsc_document_test.cpp)
    target_link_libraries(tsc_document_test PRIVATE liarsofttool_core)
    add_test(NAME tsc_document COMMAND tsc_document_test)
    add_test(NAME tsc_document_cli COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:liarsofttool> -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/tsc_document_data
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/tsc_document_test.cmake)
    target_link_libraries(tsc_check_test PRIVATE liarsofttool_core)
    add_test(NAME tsc_check COMMAND tsc_check_test)
    add_test(NAME tsc_check_cli COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:liarsofttool> -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/tsc_check_data
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/tsc_check_test.cmake)

    add_executable(gui_common_test
        tests/gui_common_test.cpp src/gui_common.cpp)
    add_test(NAME gui_common COMMAND gui_common_test)

    if(BUILD_GUI)
        find_program(XGETTEXT xgettext REQUIRED)
        find_program(MSGCMP msgcmp REQUIRED)
        add_test(NAME gui_translations COMMAND ${CMAKE_COMMAND}
            -DXGETTEXT=${XGETTEXT} -DMSGCMP=${MSGCMP}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_translations.cmake)
        add_executable(gui_smoke_test tests/gui_smoke_test.cpp src/gui_common.cpp src/gui_browser.cpp)
        add_executable(gui_browser_test tests/gui_browser_test.cpp src/gui_common.cpp)
        target_link_libraries(gui_browser_test PRIVATE liarsofttool_gui_preferences "${VORBISENC_LIBRARY}")
        add_dependencies(gui_browser_test gui_translations)
        target_link_libraries(gui_smoke_test PRIVATE liarsofttool_gui_preferences liarsofttool_updates)
        add_dependencies(gui_smoke_test gui_translations)
        foreach(language en ru zh_CN ja ja_JP fr_FR)
            add_test(NAME gui_language_${language} COMMAND gui_smoke_test ${language})
        endforeach()
        if(NOT WIN32)
            foreach(language ru zh_CN ja)
                add_test(NAME gui_standalone_${language} COMMAND ${CMAKE_COMMAND} -E env
                    LIARSOFT_TEST_EMBEDDED=1 $<TARGET_FILE:gui_smoke_test> ${language})
            endforeach()
        endif()
        if(WIN32)
            add_test(NAME gui_smoke COMMAND gui_smoke_test)
            add_test(NAME gui_browser COMMAND gui_browser_test)
        else()
            find_program(XVFB_RUN xvfb-run)
            if(XVFB_RUN)
                add_test(NAME gui_smoke COMMAND ${XVFB_RUN} -a $<TARGET_FILE:gui_smoke_test>)
                add_test(NAME gui_browser COMMAND ${XVFB_RUN} -a $<TARGET_FILE:gui_browser_test>)
            endif()
        endif()
        if(TEST gui_smoke)
            set_tests_properties(gui_smoke PROPERTIES TIMEOUT 30)
            set_tests_properties(gui_browser PROPERTIES TIMEOUT 35)
        endif()
    endif()

    add_executable(archive_test tests/archive_test.cpp)
    add_test(NAME archive COMMAND archive_test ${CMAKE_CURRENT_BINARY_DIR}/archive_test_data)
    add_executable(archive_index_test tests/archive_index_test.cpp)
    target_link_libraries(archive_index_test PRIVATE liarsofttool_core)
    add_test(NAME archive_index COMMAND archive_index_test)
    add_executable(resource_preview_test tests/resource_preview_test.cpp)
    target_link_libraries(resource_preview_test PRIVATE liarsofttool_core "${VORBISENC_LIBRARY}")
    add_test(NAME resource_preview COMMAND resource_preview_test)

    add_executable(cg_decompress_test tests/cg_decompress_test.cpp)
    add_test(NAME cg_decompress COMMAND cg_decompress_test)

    add_test(NAME directory_packing
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:liarsofttool>
            -DAUDIO_TEST=$<TARGET_FILE:wav_ogg_test>
            -DIMAGE_TEST=$<TARGET_FILE:cg_decompress_test>
            -DGUI_TEST=$<TARGET_FILE:gui_common_test>
            -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/directory_packing_test
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/directory_packing_test.cmake)
endif()

foreach(target liarsofttool liarsofttool-gui bmp_test encoding_test gscfile_test fileio_test wav_ogg_test exe_patch_test
               gsc_decompiler_test gui_common_test gui_smoke_test archive_test cg_decompress_test)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE liarsofttool_core)
        # Keep the GUI and its dependencies on the same MinGW runtime.
        if(WIN32 AND NOT target STREQUAL "liarsofttool-gui" AND NOT target STREQUAL "gui_smoke_test")
            target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
        endif()
    endif()
endforeach()
if(BUILD_TESTING)
    target_link_libraries(wav_ogg_test PRIVATE "${VORBISENC_LIBRARY}")
    target_link_libraries(gui_common_test PRIVATE "${VORBISENC_LIBRARY}")
endif()

# Separate configuration of runtime/ does not require GTK or update checks.
if(BUILD_RUNTIME)
    add_subdirectory(runtime)
endif()
