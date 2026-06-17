cmake_minimum_required(VERSION 3.10)
project(LiarsoftTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find iconv (needed on Unix; Windows uses Win32 API via encoding.cpp)
if(NOT WIN32)
    find_package(Iconv REQUIRED)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/main.cpp
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

# Executable
add_executable(liarsofttool ${SOURCES})

# Link iconv on Unix
if(NOT WIN32)
    target_link_libraries(liarsofttool PRIVATE Iconv::Iconv)
endif()

# Install
install(TARGETS liarsofttool DESTINATION bin)
