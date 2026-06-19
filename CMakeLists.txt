cmake_minimum_required(VERSION 3.10)
project(LiarsoftTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find iconv (needed on Unix; Windows uses Win32 API via encoding.cpp)
if(NOT WIN32)
    find_package(Iconv REQUIRED)
endif()

# Find GTKmm for GUI
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GTKMM gtkmm-3.0)
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
    src/gscfile.cpp
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
    add_executable(liarsofttool-gui
        src/gui_main.cpp
        src/gui_win32.cpp
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
if(GTKMM_FOUND)
    install(TARGETS liarsofttool-gui DESTINATION bin)
endif()
