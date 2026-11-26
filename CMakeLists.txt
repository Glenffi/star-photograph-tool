cmake_minimum_required(VERSION 3.20)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
   NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING
        "Minimum supported macOS version")
endif()

project(StarProcessor VERSION 0.14.0 LANGUAGES CXX)

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
    src/core/FrameSequenceOrder.cpp
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
    src/app/MainWindow.cpp
    src/app/MainWindowOrder.cpp
    src/ui/PrintOrderPanel.cpp
    resources/print-order.qrc
    resources/branding.qrc
    resources/translations.qrc
    src/i18n/AppLocalization.cpp
    src/core/ThumbnailGenerator.cpp
    src/ui/ProjectPanel.cpp
    src/ui/PreviewPanel.cpp
    src/ui/FileDialogUtils.cpp
    src/ui/Toolbar.cpp
    src/ui/ParamsPanel.cpp
    src/ui/UiAssets.cpp
    src/ui/StyleTokens.cpp
    src/ui/InspectorWidgets.cpp
    src/ui/ViewStateStore.cpp
    src/ui/TaskStatusBar.cpp
    src/ui/WorkflowRail.cpp
    src/update/UpdateManager.cpp
)

set(CORE_HEADERS
    src/core/BasicAdjustmentEngine.h
    src/core/ChromaticAberrationCorrector.h
    src/core/DeepSkyCalibrationPreflight.h
    src/core/FrameSequenceOrder.h
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
    src/app/MainWindow.h
    src/ui/PrintOrderPanel.h
    src/i18n/AppLocalization.h
    src/core/ThumbnailGenerator.h
    src/ui/ProjectPanel.h
    src/ui/PreviewPanel.h
    src/ui/FileDialogUtils.h
    src/ui/Toolbar.h
    src/ui/ParamsPanel.h
    src/ui/UiAssets.h
    src/ui/StyleTokens.h
    src/ui/InspectorWidgets.h
    src/ui/ViewStateStore.h
    src/ui/TaskStatusBar.h
    src/ui/WorkflowRail.h
    src/ui/ProcessingScene.h
    src/update/UpdateManager.h
)

if(APPLE)
    set(STARPROCESSOR_MAC_ICON
        "${CMAKE_SOURCE_DIR}/resources/StarProcessor.icns")
    set(NEBULENS_PRIVACY_MANIFEST
        "${CMAKE_SOURCE_DIR}/resources/PrivacyInfo.xcprivacy")
    set_source_files_properties(${STARPROCESSOR_MAC_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties(${NEBULENS_PRIVACY_MANIFEST} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND APP_BUNDLE_FILES
        ${STARPROCESSOR_MAC_ICON}
        ${NEBULENS_PRIVACY_MANIFEST})
elseif(WIN32)
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/StarProcessor.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/StarProcessor.rc"
        @ONLY)
    list(APPEND APP_BUNDLE_FILES "${CMAKE_CURRENT_BINARY_DIR}/StarProcessor.rc")
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
    src/workers/HistoryPreviewWorker.cpp
    src/workers/HistoryPreviewWorker.h
)
target_include_directories(StarProcessorWorkers PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(StarProcessorWorkers PUBLIC StarProcessorCore Qt6::Core Qt6::Gui)

add_library(NebulensDesktop OBJECT ${APP_SOURCES} ${APP_HEADERS})
target_include_directories(NebulensDesktop PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(NebulensDesktop PUBLIC
    StarProcessorWorkers
    Qt6::Widgets
    Qt6::Network
)
target_compile_definitions(NebulensDesktop PUBLIC
    STARPROCESSOR_VERSION="${PROJECT_VERSION}"
    PRIVATE
    STARPROCESSOR_UPDATE_MANIFEST_URL="https://di.nexusgen.net/starprocessor/update.json")

add_executable(${PROJECT_NAME} src/main.cpp ${APP_BUNDLE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE NebulensDesktop)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(NebulensWindowTests tests/MainWindowFlowTests.cpp)
    target_link_libraries(NebulensWindowTests PRIVATE NebulensDesktop)
    add_test(NAME print_order_flow
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/WindowFlowTests.py
                $<TARGET_FILE:NebulensWindowTests>)
    set_tests_properties(print_order_flow PROPERTIES TIMEOUT 150)
    add_executable(NebulensPrintOrderPanelTests tests/PrintOrderPanelTests.cpp)
    target_link_libraries(NebulensPrintOrderPanelTests PRIVATE NebulensDesktop)
    add_test(NAME print_order_panel COMMAND NebulensPrintOrderPanelTests)
    set_tests_properties(print_order_panel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(NebulensLocalizationTests
        tests/LocalizationCatalogTests.cpp
        src/i18n/AppLocalization.cpp
        src/i18n/AppLocalization.h)
    target_include_directories(NebulensLocalizationTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(NebulensLocalizationTests PRIVATE
        NEBULENS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(NebulensLocalizationTests PRIVATE Qt6::Core)
    add_test(NAME localization_catalog COMMAND NebulensLocalizationTests)

    add_executable(NebulensLocalizationDialogTests
        tests/LocalizationDialogTests.cpp
        src/i18n/AppLocalization.cpp
        src/i18n/AppLocalization.h
        src/ui/FileDialogUtils.cpp
        src/ui/FileDialogUtils.h
        src/ui/StyleTokens.cpp
        src/ui/StyleTokens.h
        src/ui/UiAssets.cpp
        src/ui/UiAssets.h
        resources/translations.qrc)
    target_include_directories(NebulensLocalizationDialogTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(NebulensLocalizationDialogTests PRIVATE
        Qt6::Widgets)
    add_test(NAME localization_dialogs
        COMMAND NebulensLocalizationDialogTests)
    set_tests_properties(localization_dialogs PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

    add_executable(StarProcessorOutputDeliveryTests tests/OutputDeliveryTests.cpp)
    target_link_libraries(StarProcessorOutputDeliveryTests PRIVATE StarProcessorWorkers)
    add_test(NAME output_delivery COMMAND StarProcessorOutputDeliveryTests)

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
        src/ui/StyleTokens.cpp
        src/ui/ViewStateStore.cpp
        src/ui/PreviewPanel.h
        src/ui/UiAssets.h
        src/ui/StyleTokens.h
        src/ui/ViewStateStore.h)
    target_include_directories(StarProcessorPreviewTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(StarProcessorPreviewTests PRIVATE
        StarProcessorCore
        Qt6::Widgets)
    add_test(NAME preview_panel COMMAND StarProcessorPreviewTests)
    set_tests_properties(preview_panel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(StarProcessorUiFoundationTests
        tests/UiFoundationTests.cpp
        src/ui/StyleTokens.cpp
        src/ui/StyleTokens.h
        src/ui/InspectorWidgets.cpp
        src/ui/InspectorWidgets.h
        src/ui/ViewStateStore.cpp
        src/ui/ViewStateStore.h
        src/ui/TaskStatusBar.cpp
        src/ui/TaskStatusBar.h
        src/ui/WorkflowRail.cpp
        src/ui/WorkflowRail.h
        src/ui/ProcessingScene.h)
    target_include_directories(StarProcessorUiFoundationTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(StarProcessorUiFoundationTests PRIVATE Qt6::Widgets)
    add_test(NAME ui_foundation COMMAND StarProcessorUiFoundationTests)
    set_tests_properties(ui_foundation PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(StarProcessorProjectPanelTests
        tests/ProjectPanelTests.cpp
        src/core/ThumbnailGenerator.cpp
        src/core/ThumbnailGenerator.h
        src/ui/ProjectPanel.cpp
        src/ui/ProjectPanel.h
        src/ui/UiAssets.cpp
        src/ui/UiAssets.h
        src/ui/StyleTokens.cpp
        src/ui/StyleTokens.h
        src/ui/ProcessingScene.h)
    target_include_directories(StarProcessorProjectPanelTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(StarProcessorProjectPanelTests PRIVATE
        StarProcessorCore Qt6::Widgets)
    add_test(NAME project_panel COMMAND StarProcessorProjectPanelTests)
    set_tests_properties(project_panel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(StarProcessorParamsPanelTests
        tests/ParamsPanelTests.cpp
        src/ui/ParamsPanel.cpp
        src/ui/ParamsPanel.h
        src/ui/FileDialogUtils.cpp
        src/ui/FileDialogUtils.h
        src/ui/UiAssets.cpp
        src/ui/UiAssets.h
        src/ui/StyleTokens.cpp
        src/ui/StyleTokens.h
        src/ui/InspectorWidgets.cpp
        src/ui/InspectorWidgets.h
        src/ui/ProcessingScene.h)
    target_include_directories(StarProcessorParamsPanelTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(StarProcessorParamsPanelTests PRIVATE
        StarProcessorCore Qt6::Widgets)
    add_test(NAME params_panel COMMAND StarProcessorParamsPanelTests)
    set_tests_properties(params_panel PROPERTIES
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
    if(BUILD_TESTING)
        add_test(NAME timelapse_cli
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/TimelapseCliTests.py
                    $<TARGET_FILE:StarProcessorPipelineRunner>)
        if(WIN32)
            add_test(NAME timelapse_windows_volume
                COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_SOURCE_DIR}/tests/TimelapseWindowsVolumeTests.py
                        $<TARGET_FILE:StarProcessorPipelineRunner>)
        endif()
    endif()
endif()

# macOS 特定设置
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Nebulens"
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
