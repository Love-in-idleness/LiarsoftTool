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
