cmake_minimum_required(VERSION 3.20)
project(StarProcessor VERSION 0.4.0 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

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

# 定义源文件
set(SOURCES
    src/main.cpp
    src/core/RawImageLoader.cpp
    src/core/ThumbnailGenerator.cpp
    src/core/StarDetector.cpp
    src/core/ImageAligner.cpp
    src/core/StackingEngine.cpp
    src/core/ImageExporter.cpp
    src/core/AutoOptimizeEngine.cpp
    src/core/PresetManager.cpp
    src/ui/ProjectPanel.cpp
    src/ui/PreviewPanel.cpp
    src/ui/Toolbar.cpp
    src/ui/ParamsPanel.cpp
)

set(HEADERS
    src/core/RawImageLoader.h
    src/core/ThumbnailGenerator.h
    src/core/StarDetector.h
    src/core/ImageAligner.h
    src/core/StackingEngine.h
    src/core/ImageExporter.h
    src/core/AutoOptimizeEngine.h
    src/core/PresetManager.h
    src/ui/ProjectPanel.h
    src/ui/PreviewPanel.h
    src/ui/Toolbar.h
    src/ui/ParamsPanel.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 条件定义 libtiff 可用性
if(TIFF_LIBRARY AND TIFF_INCLUDE_DIR)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_LIBTIFF)
endif()

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LibRaw_INCLUDE_DIR}
    ${TIFF_INCLUDE_DIR}
)

# 链接 Qt 库和 LibRaw、libtiff
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${LibRaw_LIBRARY}
    ${TIFF_LIBRARY}
)

# macOS 特定设置
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "StarProcessor"
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
