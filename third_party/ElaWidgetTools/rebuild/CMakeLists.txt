cmake_minimum_required(VERSION 3.16)
project(AttendanceElaSdk LANGUAGES CXX)

set(ELA_SOURCE_DIR "" CACHE PATH "ElaWidgetTools source directory containing ElaWindow.cpp")
if(NOT EXISTS "${ELA_SOURCE_DIR}/ElaWindow.cpp")
    message(FATAL_ERROR "Set ELA_SOURCE_DIR to the shared ElaWidgetTools source directory")
endif()
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The bundled SDK requires Windows MSVC and Qt 5")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
set(QT_VERSION_MAJOR 5)
add_compile_options(/utf-8)
set(ELAWIDGETTOOLS_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
add_subdirectory("${ELA_SOURCE_DIR}" ela)

# Only the explicit refresh_sdk target replaces the bundled release SDK.
get_filename_component(SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(GLOB SDK_HEADERS "${ELA_SOURCE_DIR}/*.h")
add_custom_target(refresh_sdk
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ElaWidgetTools> "${SDK_DIR}/bin/ElaWidgetTools.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_LINKER_FILE:ElaWidgetTools> "${SDK_DIR}/lib/ElaWidgetTools.lib"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDK_HEADERS} "${SDK_DIR}/include"
    DEPENDS ElaWidgetTools
    COMMAND_EXPAND_LISTS
    VERBATIM
)
