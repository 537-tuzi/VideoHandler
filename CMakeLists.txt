cmake_minimum_required(VERSION 3.21)

project(video_handler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_AUTOMOC ON)

# Qt：Core（引擎层 QString）+ Widgets（GUI）
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# ---- engine 静态库：全部流式引擎 + 转码/提取底座（GUI 与 engine_test 共用）----
add_library(engine STATIC
    src/service/aidenoiser.h
    src/service/aidenoiser.cpp
    src/service/commandlayer.h
    src/service/commandlayer.cpp
    src/service/dspdenoiser.h
    src/service/dspdenoiser.cpp
    src/service/engineregistry.h
    src/service/engineregistry.cpp
    src/service/errinfo.h
    src/service/ffextractor.h
    src/service/ffextractor.cpp
    src/service/fftranscoder.h
    src/service/fftranscoder.cpp
    src/service/pitchshifter.h
    src/service/pitchshifter.cpp
    src/service/streamengine.h
    src/service/vocalseparator.h
    src/service/vocalseparator.cpp
    src/service/workspace.h
    src/service/workspace.cpp
    src/model/data.h
    src/util/ffutil.h
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(engine PRIVATE -Wall -Wextra)

if(WIN32)
    # 产品接线：third_party 四套（README 获取）；MinGW 专属选项不外溢到 Linux
    target_include_directories(engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/soundtouch/include
    )
    target_link_libraries(engine
        PUBLIC Qt6::Core
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib/libavformat-63.a
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib/libavcodec-63.a
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib/libavutil-61.a
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin/swresample-7.dll# mingw ld 可直连 DLL 免导入库
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/lib/libonnxruntime.a
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/soundtouch/lib/libSoundTouch.a
    )
    target_compile_definitions(engine PRIVATE MINGW_HAS_SECURE_API=1)
    target_compile_options(engine PRIVATE -fno-keep-inline-dllexport -mthreads)
else()
    # Linux 测试构建（linux-test preset）：FFmpeg/SoundTouch 用 apt（pkg-config），
    # ORT 用 scripts/fetch-deps.sh 解出的官方 1.30.0 到 third_party/ort/linux（与 Windows 同版本）
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
    pkg_check_modules(SOUNDTOUCH REQUIRED IMPORTED_TARGET soundtouch)
    target_include_directories(engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/linux/include)
    target_link_libraries(engine
        PUBLIC Qt6::Core
        PRIVATE
        PkgConfig::FFMPEG
        PkgConfig::SOUNDTOUCH
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/linux/lib/libonnxruntime.so
    )
    # 构建目录内可执行文件直跑 ORT .so（无需 LD_LIBRARY_PATH）
    set(CMAKE_BUILD_RPATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/linux/lib)
endif()

# ---- GUI 应用 ----
add_executable(video_handler WIN32
    src/main.cpp
    src/service/mediaservice.h
    src/service/mediaservice.cpp
    src/view/mainwidget.h
    src/view/mainwidget.cpp
)
target_link_libraries(video_handler PRIVATE engine Qt6::Widgets)
target_compile_options(video_handler PRIVATE -Wall -Wextra)

if(WIN32)
    target_compile_definitions(video_handler PRIVATE MINGW_HAS_SECURE_API=1)
    target_compile_options(video_handler PRIVATE -fno-keep-inline-dllexport -mthreads)
    target_link_options(video_handler PRIVATE
        -mthreads
        $<$<CONFIG:Release>:-s># 仅 Release 剥符号，Debug 保留符号供 gdb
    )

    # Release 产物落 src/bin，Debug 独立目录防覆盖
    set_target_properties(video_handler PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/src/bin-dbg
    )

    add_custom_command(TARGET video_handler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin/avcodec-63.dll
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin/avformat-63.dll
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin/avutil-61.dll
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin/swresample-7.dll
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/bin/onnxruntime.dll
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/bin/onnxruntime_providers_shared.dll
            $<TARGET_FILE_DIR:video_handler>
    )

    add_custom_command(TARGET video_handler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:video_handler>/models/denoiser
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/denoiser/gtcrn_simple.onnx
            $<TARGET_FILE_DIR:video_handler>/models/denoiser/
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:video_handler>/models/separator
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
            $<TARGET_FILE_DIR:video_handler>/models/separator/
    )

    # 自包含部署：Qt/MinGW 运行时跟随 exe（windeployqt 在本环境管道异常，手动极简部署）
    # QT_BIN/MINGW_BIN 为 cache 变量（preset 提供默认，命令行 -D 可覆盖——消灭 D: 硬编码，NFR-M4 前置）
    set(QT_BIN "" CACHE PATH "Qt mingw_64 根目录（bin/plugins 相对此）")
    set(MINGW_BIN "" CACHE PATH "MinGW 工具链 bin 目录（运行时 DLL）")
    add_custom_command(TARGET video_handler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QT_BIN}/bin/Qt6Core.dll
            ${QT_BIN}/bin/Qt6Gui.dll
            ${QT_BIN}/bin/Qt6Widgets.dll
            ${MINGW_BIN}/libgcc_s_seh-1.dll
            ${MINGW_BIN}/libstdc++-6.dll
            ${MINGW_BIN}/libwinpthread-1.dll
            $<TARGET_FILE_DIR:video_handler>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:video_handler>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QT_BIN}/plugins/platforms/qwindows.dll
            $<TARGET_FILE_DIR:video_handler>/platforms/
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:video_handler>/styles
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QT_BIN}/plugins/styles/qmodernwindowsstyle.dll
            $<TARGET_FILE_DIR:video_handler>/styles/
    )
endif()

# ---- engine_test / fft_test：无头 DUT（引擎分块不变性 + FFT 属性），仅非 WIN32 ----
if(NOT WIN32)
    add_executable(engine_test src/test/engine_test.cpp)
    target_link_libraries(engine_test PRIVATE engine)
    target_compile_options(engine_test PRIVATE -Wall -Wextra)

    add_executable(fft_test src/test/fft_test.cpp)
    target_link_libraries(fft_test PRIVATE engine)
    target_compile_options(fft_test PRIVATE -Wall -Wextra)
endif()
