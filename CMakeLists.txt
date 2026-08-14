cmake_minimum_required(VERSION 3.20)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
   NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING
        "Minimum supported macOS version")
endif()

project(StarProcessor VERSION 0.10.2 LANGUAGES CXX)

include(CTest)
option(BUILD_SAMPLE_TOOLS "Build local RAW sample regression tools" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 自动处理 Qt 的 moc/uic/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 跨平台编译优化
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_options(/MP)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 查找 Qt6 组件
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# 查找 LibRaw（支持跨平台）
# 优先级：1) 环境变量 LIBRAW_ROOT  2) 常见包管理器路径  3) 系统默认路径
find_library(LibRaw_LIBRARY 
    NAMES raw libraw
    HINTS
        $ENV{LIBRAW_ROOT}/lib
        /opt/homebrew/opt/libraw/lib
        /usr/local/opt/libraw/lib
        ${CMAKE_PREFIX_PATH}/lib
)
find_path(LibRaw_INCLUDE_DIR 
    libraw/libraw.h
    HINTS
        $ENV{LIBRAW_ROOT}/include
        /opt/homebrew/opt/libraw/include
        /usr/local/opt/libraw/include
        ${CMAKE_PREFIX_PATH}/include
)

if(NOT LibRaw_LIBRARY)
    message(FATAL_ERROR "LibRaw 库未找到。请确保已通过 brew 安装：brew install libraw")
endif()

if(NOT LibRaw_INCLUDE_DIR)
    message(FATAL_ERROR "LibRaw 头文件未找到。请确保已通过 brew 安装：brew install libraw")
endif()

message(STATUS "LibRaw 库: ${LibRaw_LIBRARY}")
message(STATUS "LibRaw 头文件: ${LibRaw_INCLUDE_DIR}")

# 查找 libtiff（用于图像导出）
find_package(TIFF QUIET)
if(TIFF_FOUND)
    set(TIFF_LIBRARY ${TIFF_LIBRARIES})
    set(TIFF_INCLUDE_DIR ${TIFF_INCLUDE_DIRS})
    message(STATUS "libtiff (find_package): ${TIFF_LIBRARY}")
else()
    find_library(TIFF_LIBRARY
        NAMES tiff
        HINTS
            /opt/homebrew/opt/libtiff/lib
            /usr/local/opt/libtiff/lib
            ${CMAKE_PREFIX_PATH}/lib
    )
    find_path(TIFF_INCLUDE_DIR
        tiffio.h
        HINTS
            /opt/homebrew/opt/libtiff/include
            /usr/local/opt/libtiff/include
            ${CMAKE_PREFIX_PATH}/include
    )
if(NOT TIFF_LIBRARY OR NOT TIFF_INCLUDE_DIR)
    message(FATAL_ERROR "libtiff 未找到（库或头文件缺失）。TIFF 16-bit 导出是 P1 必需功能，请安装 libtiff。\n"
        "macOS: brew install libtiff\n"
        "Windows: vcpkg install tiff\n"
        "Linux: apt-get install libtiff-dev")
endif()
message(STATUS "libtiff 库: ${TIFF_LIBRARY}")
message(STATUS "libtiff 头文件: ${TIFF_INCLUDE_DIR}")
endif()

# 核心算法单独成库：GUI 与测试复用同一份实现，避免测试只覆盖复制代码。
set(CORE_SOURCES
    src/core/BasicAdjustmentEngine.cpp
    src/core/ChromaticAberrationCorrector.cpp
    src/core/DeepSkyCalibrationPreflight.cpp
    src/core/ImageBufferUtils.cpp
    src/core/MasterFrameIO.cpp
    src/core/MinimumFilter.cpp
    src/core/NoiseReductionEngine.cpp
    src/core/PhotometricNormalizer.cpp
    src/core/ProcessingMemoryEstimator.cpp
    src/core/PreviewToneMapper.cpp
    src/core/RawImageLoader.cpp
    src/core/StarDetector.cpp
    src/core/SkyGroundMask.cpp
    src/core/ImageAligner.cpp
    src/core/StackingEngine.cpp
    src/core/ImageExporter.cpp
    src/core/AutoOptimizeEngine.cpp
    src/core/FrameQualityEvaluator.cpp
    src/core/FinishingPipeline.cpp
    src/core/PresetManager.cpp
    src/core/RawCalibrationEngine.cpp
    src/core/StarReducer.cpp
    src/core/StarTrailEngine.cpp
    src/core/TimelapseEngine.cpp
    src/core/TemporalPhotometricSmoother.cpp
    src/core/UpdateManifest.cpp
    src/core/VerifiedDownloadFinalizer.cpp
)

set(APP_SOURCES
    src/main.cpp
    src/core/ThumbnailGenerator.cpp
    src/ui/ProjectPanel.cpp
    src/ui/PreviewPanel.cpp
    src/ui/Toolbar.cpp
    src/ui/ParamsPanel.cpp
    src/ui/UiAssets.cpp
    src/ui/SceneLauncher.cpp
    src/update/UpdateManager.cpp
)

set(CORE_HEADERS
    src/core/BasicAdjustmentEngine.h
    src/core/ChromaticAberrationCorrector.h
    src/core/DeepSkyCalibrationPreflight.h
    src/core/ImageBufferUtils.h
    src/core/MasterFrameIO.h
    src/core/MinimumFilter.h
    src/core/NoiseReductionEngine.h
    src/core/PhotometricNormalizer.h
    src/core/ProcessingMemoryEstimator.h
    src/core/PreviewToneMapper.h
    src/core/RawImageLoader.h
    src/core/StarDetector.h
    src/core/SkyGroundMask.h
    src/core/ImageAligner.h
    src/core/StackingEngine.h
    src/core/ImageExporter.h
    src/core/AutoOptimizeEngine.h
    src/core/FrameQualityEvaluator.h
    src/core/FinishingPipeline.h
    src/core/PresetManager.h
    src/core/RawCalibrationEngine.h
    src/core/StarReducer.h
    src/core/StarTrailEngine.h
    src/core/TimelapseEngine.h
    src/core/TemporalPhotometricSmoother.h
    src/core/UpdateManifest.h
    src/core/VerifiedDownloadFinalizer.h
)

set(APP_HEADERS
    src/core/ThumbnailGenerator.h
    src/ui/ProjectPanel.h
    src/ui/PreviewPanel.h
    src/ui/Toolbar.h
    src/ui/ParamsPanel.h
    src/ui/UiAssets.h
    src/ui/SceneLauncher.h
    src/update/UpdateManager.h
)

if(APPLE)
    set(STARPROCESSOR_MAC_ICON
        "${CMAKE_SOURCE_DIR}/resources/StarProcessor.icns")
    set_source_files_properties(${STARPROCESSOR_MAC_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND APP_SOURCES ${STARPROCESSOR_MAC_ICON})
elseif(WIN32)
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/StarProcessor.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/StarProcessor.rc"
        @ONLY)
    list(APPEND APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/StarProcessor.rc")
endif()

# 核心库不依赖 Widgets；ImageExporter、PresetManager 和 SkyGroundMask
# 使用 Qt Core/Gui 的文件、色彩空间和图像能力。
add_library(StarProcessorCore STATIC ${CORE_SOURCES} ${CORE_HEADERS})

target_compile_definitions(StarProcessorCore PUBLIC HAS_LIBTIFF)

# PUBLIC 使应用和测试获得一致的第三方头文件契约。
target_include_directories(StarProcessorCore PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${LibRaw_INCLUDE_DIR}
    ${TIFF_INCLUDE_DIR}
)

# 链接 Qt 库和 LibRaw、libtiff
target_link_libraries(StarProcessorCore PUBLIC
    Qt6::Core
    Qt6::Gui
    ${LibRaw_LIBRARY}
    ${TIFF_LIBRARY}
)

add_library(StarProcessorWorkers STATIC
    src/workers/ProcessingWorker.cpp
    src/workers/ProcessingWorker.h
    src/workers/ExportWorker.cpp
    src/workers/ExportWorker.h
    src/workers/MaskPreviewWorker.cpp
    src/workers/MaskPreviewWorker.h
    src/workers/QuickPreviewWorker.cpp
    src/workers/QuickPreviewWorker.h
)
target_include_directories(StarProcessorWorkers PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(StarProcessorWorkers PUBLIC StarProcessorCore Qt6::Core Qt6::Gui)

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${APP_HEADERS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE
    StarProcessorWorkers
    Qt6::Widgets
    Qt6::Network
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    STARPROCESSOR_VERSION="${PROJECT_VERSION}"
    STARPROCESSOR_UPDATE_MANIFEST_URL="https://di.nexusgen.net/starprocessor/update.json")

if(BUILD_TESTING)
    add_executable(StarProcessorBasicAdjustmentTests
        tests/BasicAdjustmentEngineTests.cpp)
    target_link_libraries(StarProcessorBasicAdjustmentTests
        PRIVATE StarProcessorCore)
    add_test(NAME basic_adjustments
        COMMAND StarProcessorBasicAdjustmentTests)

    add_executable(StarProcessorCoreTests tests/CoreTests.cpp)
    target_link_libraries(StarProcessorCoreTests PRIVATE StarProcessorCore)
    add_test(NAME core_algorithms COMMAND StarProcessorCoreTests)

    add_executable(StarProcessorWorkerTests tests/WorkerTests.cpp)
    target_link_libraries(StarProcessorWorkerTests PRIVATE StarProcessorWorkers)
    add_test(NAME worker_lifecycle COMMAND StarProcessorWorkerTests)

    add_executable(StarProcessorFinishingPipelineTests
        tests/FinishingPipelineTests.cpp)
    target_link_libraries(StarProcessorFinishingPipelineTests
        PRIVATE StarProcessorCore)
    add_test(NAME finishing_pipeline
        COMMAND StarProcessorFinishingPipelineTests)

    add_executable(StarProcessorStarTrailTests
        tests/StarTrailEngineTests.cpp)
    target_link_libraries(StarProcessorStarTrailTests PRIVATE StarProcessorCore)
    add_test(NAME star_trail_engine COMMAND StarProcessorStarTrailTests)

    add_executable(StarProcessorPreviewTests
        tests/PreviewPanelTests.cpp
        src/ui/PreviewPanel.cpp
        src/ui/UiAssets.cpp
        src/ui/PreviewPanel.h
        src/ui/UiAssets.h)
    target_include_directories(StarProcessorPreviewTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(StarProcessorPreviewTests PRIVATE
        StarProcessorCore
        Qt6::Widgets)
    add_test(NAME preview_panel COMMAND StarProcessorPreviewTests)
    set_tests_properties(preview_panel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_SAMPLE_TOOLS)
    add_executable(StarProcessorSampleRegression tools/RawSampleRegression.cpp)
    target_compile_definitions(StarProcessorSampleRegression PRIVATE
        STARPROCESSOR_VERSION="${PROJECT_VERSION}")
    target_link_libraries(StarProcessorSampleRegression PRIVATE StarProcessorCore)

    add_executable(StarProcessorPipelineRunner tools/RawPipelineRunner.cpp)
    target_compile_definitions(StarProcessorPipelineRunner PRIVATE
        STARPROCESSOR_VERSION="${PROJECT_VERSION}")
    target_link_libraries(StarProcessorPipelineRunner PRIVATE StarProcessorWorkers)
endif()

# macOS 特定设置
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "StarProcessor"
        MACOSX_BUNDLE_ICON_FILE "StarProcessor.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# Windows 特定设置
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# 安装规则
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
