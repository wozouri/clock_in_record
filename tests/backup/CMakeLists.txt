cmake_minimum_required(VERSION 3.16)
project(BackupTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Sql Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Sql Test)
add_executable(backup_tests BackupTests.cpp
    ../../service/BackupStore.cpp
    ../../src/Backup/BackupClient.cpp
    ../../src/Data/AttendanceStorage.cpp
    ../../src/Update/UpdateChecker.cpp
    ../../src/Update/ClientVersion.cpp)
set_target_properties(backup_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_include_directories(backup_tests PRIVATE ../../src ../../service)
target_link_libraries(backup_tests PRIVATE Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Test)
if(MSVC)
    target_compile_options(backup_tests PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME backup COMMAND backup_tests)
set_tests_properties(backup PROPERTIES TIMEOUT 90
    ENVIRONMENT "ATTENDANCE_BACKUP_TEST_SERVICE=$<TARGET_FILE:AttendanceUpdateService>")
