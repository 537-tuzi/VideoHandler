# Linux 测试构建（WP-1.2 的种子，2026-10-14 架构审视热修验证用）
# 独立存在的原因：产品 CMakeLists 为 MinGW 专用接线（third_party .a/DLL 绝对路径、-mthreads、
# POST_BUILD 拷 DLL），Linux 上不可用；WP-1.2 CMake 分层落地后本文件退役。
# 用法：
#   cmake -B build-post -DVH_SRC_DIR=<主仓库>/src -G Ninja && cmake --build build-post
#   cmake -B build-pre -DVH_SRC_DIR=/home/psh/vh-prefix/src -G Ninja && cmake --build build-pre
cmake_minimum_required(VERSION 3.21)
project(video_handler_linux_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 待测源码目录：默认指向主仓库 src；对照构建时用 -DVH_SRC_DIR 指到修复前 worktree
set(VH_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../src" CACHE PATH "待测源码目录")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
pkg_check_modules(SOUNDTOUCH REQUIRED IMPORTED_TARGET soundtouch)

add_executable(video_handler
    ${VH_SRC_DIR}/main.cpp
    ${VH_SRC_DIR}/service/aidenoiser.h
    ${VH_SRC_DIR}/service/aidenoiser.cpp
    ${VH_SRC_DIR}/service/dspdenoiser.h
    ${VH_SRC_DIR}/service/dspdenoiser.cpp
    ${VH_SRC_DIR}/service/ffextractor.h
    ${VH_SRC_DIR}/service/ffextractor.cpp
    ${VH_SRC_DIR}/service/fftranscoder.h
    ${VH_SRC_DIR}/service/fftranscoder.cpp
    ${VH_SRC_DIR}/service/mediaservice.h
    ${VH_SRC_DIR}/service/mediaservice.cpp
    ${VH_SRC_DIR}/service/pitchshifter.h
    ${VH_SRC_DIR}/service/pitchshifter.cpp
    ${VH_SRC_DIR}/service/streamengine.h
    ${VH_SRC_DIR}/service/vocalseparator.h
    ${VH_SRC_DIR}/service/vocalseparator.cpp
    ${VH_SRC_DIR}/view/mainwidget.h
    ${VH_SRC_DIR}/view/mainwidget.cpp
    ${VH_SRC_DIR}/model/data.h
    ${VH_SRC_DIR}/util/ffutil.h
)

target_include_directories(video_handler PRIVATE
    ${VH_SRC_DIR}
    /usr/include/onnxruntime
    /usr/include/soundtouch
)

target_link_libraries(video_handler PRIVATE
    Qt6::Widgets
    PkgConfig::FFMPEG
    PkgConfig::SOUNDTOUCH
    onnxruntime
)

target_compile_options(video_handler PRIVATE -Wall -Wextra)
