cmake_minimum_required(VERSION 3.21)

project(video_handler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_AUTOMOC ON)

# Qt：Widgets
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(video_handler WIN32
    src/main.cpp
    # service
    src/service/aidenoiser.h
    src/service/aidenoiser.cpp
    src/service/dspdenoiser.h
    src/service/dspdenoiser.cpp
    src/service/ffextractor.h
    src/service/ffextractor.cpp
    src/service/fftranscoder.h
    src/service/fftranscoder.cpp
    src/service/mediaservice.h
    src/service/mediaservice.cpp
    src/service/pitchshifter.h
    src/service/pitchshifter.cpp
    src/service/streamengine.h
    src/service/vocalseparator.h
    src/service/vocalseparator.cpp
    # view
    src/view/mainwidget.h
    src/view/mainwidget.cpp
    # headers（AUTOMOC 扫描 Q_OBJECT）
    src/model/data.h
    src/util/ffutil.h
)

target_include_directories(video_handler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/soundtouch/include
)

target_link_libraries(video_handler PRIVATE
    Qt6::Widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib/libavformat-63.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib/libavcodec-63.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib/libavutil-61.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/bin/swresample-7.dll# mingw ld 可直连 DLL 免导入库
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ort/lib/libonnxruntime.a
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/soundtouch/lib/libSoundTouch.a
)

target_compile_definitions(video_handler PRIVATE
    MINGW_HAS_SECURE_API=1
)

target_compile_options(video_handler PRIVATE
    -Wall -Wextra
    -fno-keep-inline-dllexport
    -mthreads
)

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
set(QT_BIN "D:/qt6/6.11.1/mingw_64")
set(MINGW_BIN "D:/qt6/Tools/mingw1310_64/bin")
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
