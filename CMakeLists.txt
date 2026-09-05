cmake_minimum_required(VERSION 3.16)
project(AttendanceApp VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt库
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Sql)

# Qt MOC设置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 编译器特定设置
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 添加src子目录
add_subdirectory(third_party/ElaWidgetTools)
add_subdirectory(src)
