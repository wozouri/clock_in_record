cmake_minimum_required(VERSION 3.16)
project(SingleInstanceTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Test)
add_executable(single_instance_tests
    SingleInstanceTests.cpp
    ../../src/Utils/SingleInstanceGuard.cpp
    ../../src/Utils/SingleInstanceGuard.h)
target_include_directories(single_instance_tests PRIVATE ../../src/Utils)
target_link_libraries(single_instance_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test)
if(MSVC)
    target_compile_options(single_instance_tests PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME single_instance COMMAND single_instance_tests)
