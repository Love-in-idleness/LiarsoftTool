cmake_minimum_required(VERSION 3.10)
project(LiarsoftTool VERSION 2.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Find GTKmm for the Linux GUI
if(NOT WIN32)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTKMM gtkmm-3.0)
    endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Windows resource file (manifest for visual styles + DPI awareness)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Find windres (prefixed with cross-compiler triplet, e.g. x86_64-w64-mingw32-windres)
    get_filename_component(_MINGW_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    get_filename_component(_MINGW_CXX ${CMAKE_CXX_COMPILER} NAME)
    string(REGEX REPLACE "-g\\+\\+$" "" _MINGW_PFX "${_MINGW_CXX}")
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

# Common source files (shared between CLI and GUI)
set(COMMON_SOURCES
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
    src/exe_patch.cpp
    src/stb_impl.cpp
    src/stb_image_impl.cpp
)

# ---- CLI target ----
add_executable(liarsofttool
    src/cli_main.cpp
    ${COMMON_SOURCES}
)
if(NOT WIN32)
    target_link_libraries(liarsofttool PRIVATE Iconv::Iconv)
else()
    target_sources(liarsofttool PRIVATE ${RESOURCE_OBJ})
    target_link_options(liarsofttool PRIVATE -static-libgcc -static-libstdc++)
endif()

# ---- GUI target ----
if(WIN32)
    # Windows: use native Win32 GUI (no extra deps)
    add_executable(liarsofttool-gui WIN32
        src/gui_main.cpp
        src/gui_win32.cpp
        src/gui_common.cpp
        ${COMMON_SOURCES}
    )
    target_link_libraries(liarsofttool-gui PRIVATE comctl32 comdlg32 ole32)
    target_sources(liarsofttool-gui PRIVATE ${RESOURCE_OBJ})
    target_link_options(liarsofttool-gui PRIVATE -static-libgcc -static-libstdc++)
elseif(GTKMM_FOUND)
    # Linux: use GTKmm GUI
    add_executable(liarsofttool-gui
        src/gui_main.cpp
        src/gui.cpp
        src/gui_common.cpp
        ${COMMON_SOURCES}
    )
    target_include_directories(liarsofttool-gui PRIVATE ${GTKMM_INCLUDE_DIRS})
    target_link_libraries(liarsofttool-gui PRIVATE ${GTKMM_LIBRARIES} Iconv::Iconv)
    target_compile_options(liarsofttool-gui PRIVATE ${GTKMM_CFLAGS_OTHER})
    message(STATUS "GTK GUI: enabled (liarsofttool-gui)")
else()
    message(STATUS "GUI: disabled (no GTKmm on Linux, Win32 API on Windows)")
endif()

# Install
install(TARGETS liarsofttool DESTINATION bin)
install(FILES docs/THIRD_PARTY_AUDIO_LICENSES.md DESTINATION share/doc/LiarsoftTool)
if(WIN32 OR GTKMM_FOUND)
    install(TARGETS liarsofttool-gui DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    find_library(VORBISENC_LIBRARY NAMES vorbisenc)
    if(NOT VORBISENC_LIBRARY)
        message(FATAL_ERROR "libvorbisenc is required to generate audio test inputs")
    endif()
    add_executable(encoding_test tests/encoding_test.cpp src/encoding.cpp)
    if(NOT WIN32)
        target_link_libraries(encoding_test PRIVATE Iconv::Iconv)
    else()
        target_link_options(encoding_test PRIVATE -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME encoding COMMAND encoding_test)

    add_executable(wav_ogg_test
        tests/wav_ogg_test.cpp src/wav_ogg.cpp src/fileio.cpp)
    if(WIN32)
        target_link_options(wav_ogg_test PRIVATE -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME wav_ogg COMMAND wav_ogg_test)

    add_executable(exe_patch_test
        tests/exe_patch_test.cpp src/exe_patch.cpp src/fileio.cpp)
    if(WIN32)
        target_link_options(exe_patch_test PRIVATE -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME exe_patch COMMAND exe_patch_test)

    add_executable(gsc_decompiler_test
        tests/gsc_decompiler_test.cpp src/gsc_decompiler.cpp
        src/encoding.cpp src/fileio.cpp)
    if(NOT WIN32)
        target_link_libraries(gsc_decompiler_test PRIVATE Iconv::Iconv)
    else()
        target_link_options(gsc_decompiler_test PRIVATE -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME gsc_decompiler COMMAND gsc_decompiler_test)

    add_executable(gui_common_test
        tests/gui_common_test.cpp src/gui_common.cpp ${COMMON_SOURCES})
    if(NOT WIN32)
        target_link_libraries(gui_common_test PRIVATE Iconv::Iconv)
    else()
        target_link_options(gui_common_test PRIVATE -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME gui_common COMMAND gui_common_test)

    add_executable(archive_test tests/archive_test.cpp ${COMMON_SOURCES})
    if(NOT WIN32)
        target_link_libraries(archive_test PRIVATE Iconv::Iconv)
    else()
        target_link_options(archive_test PRIVATE -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME archive COMMAND archive_test ${CMAKE_CURRENT_BINARY_DIR}/archive_test_data)

    add_executable(cg_decompress_test
        tests/cg_decompress_test.cpp src/cg_decompress.cpp src/wcg_decoder.cpp
        src/lim_decoder.cpp src/fileio.cpp src/stb_impl.cpp)
    if(WIN32)
        target_link_options(cg_decompress_test PRIVATE -static-libgcc -static-libstdc++)
    endif()
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

foreach(target liarsofttool liarsofttool-gui gui_common_test archive_test cg_decompress_test)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE WebP::webp)
    endif()
endforeach()

foreach(target liarsofttool liarsofttool-gui wav_ogg_test gui_common_test archive_test)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE Vorbis::file)
    endif()
endforeach()
if(BUILD_TESTING)
    target_link_libraries(wav_ogg_test PRIVATE "${VORBISENC_LIBRARY}")
    target_link_libraries(gui_common_test PRIVATE "${VORBISENC_LIBRARY}")
endif()
