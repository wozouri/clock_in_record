cmake_minimum_required(VERSION 3.16)
project(StatsTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Sql Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql Test)
add_executable(stats_tests StatsTests.cpp
    ../../src/Data/AttendanceStatsService.cpp
    ../../src/Data/AttendanceStorage.cpp
    ../../src/Data/AttendanceJsonService.cpp
    ../../src/Cal/WorkTimeCalculator.cpp)
set_target_properties(stats_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_include_directories(stats_tests PRIVATE ../../src ../../src/Types)
target_link_libraries(stats_tests PRIVATE Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Test)
if(MSVC)
    target_compile_options(stats_tests PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME monthly_overtime_targets COMMAND stats_tests -o -,txt)
