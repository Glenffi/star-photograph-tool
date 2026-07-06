cmake_minimum_required(VERSION 3.20)
project(StarProcessor VERSION 0.2.0 LANGUAGES CXX)

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

# 查找 LibRaw（brew 安装路径）
find_library(LibRaw_LIBRARY 
    NAMES raw libraw
    PATHS /opt/homebrew/opt/libraw/lib
    NO_DEFAULT_PATH
)
find_path(LibRaw_INCLUDE_DIR 
    libraw/libraw.h
    PATHS /opt/homebrew/opt/libraw/include
    NO_DEFAULT_PATH
)

if(NOT LibRaw_LIBRARY)
    message(FATAL_ERROR "LibRaw 库未找到。请确保已通过 brew 安装：brew install libraw")
endif()

if(NOT LibRaw_INCLUDE_DIR)
    message(FATAL_ERROR "LibRaw 头文件未找到。请确保已通过 brew 安装：brew install libraw")
endif()

message(STATUS "LibRaw 库: ${LibRaw_LIBRARY}")
message(STATUS "LibRaw 头文件: ${LibRaw_INCLUDE_DIR}")

# 定义源文件
set(SOURCES
    src/main.cpp
    src/core/RawImageLoader.cpp
    src/core/ThumbnailGenerator.cpp
    src/ui/ProjectPanel.cpp
    src/ui/PreviewPanel.cpp
    src/ui/Toolbar.cpp
    src/ui/ParamsPanel.cpp
)

set(HEADERS
    src/core/RawImageLoader.h
    src/core/ThumbnailGenerator.h
    src/ui/ProjectPanel.h
    src/ui/PreviewPanel.h
    src/ui/Toolbar.h
    src/ui/ParamsPanel.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LibRaw_INCLUDE_DIR}
)

# 链接 Qt 库和 LibRaw
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${LibRaw_LIBRARY}
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
