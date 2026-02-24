# 自动收集源文件
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "Utils/*.cpp"
    "Cal/*.cpp"
)
file(GLOB_RECURSE APP_HEADERS CONFIGURE_DEPENDS
    "*.h"
    "Utils/*.h"
    "Cal/*.h"
)

# 暂时停用更新模块
list(FILTER APP_SOURCES EXCLUDE REGEX ".*/UpdateChecker/.*")
list(FILTER APP_HEADERS EXCLUDE REGEX ".*/UpdateChecker/.*")
# 收集类型定义头文件
file(GLOB_RECURSE TYPE_HEADERS CONFIGURE_DEPENDS "Types/*.h")

# 资源文件配置
set(RESOURCE_DIR "${CMAKE_SOURCE_DIR}/resources")
set(APP_RESOURCES "${RESOURCE_DIR}/resources.qrc")

# Windows特定资源文件处理
set(WIN_RESOURCES "")
if(WIN32)
    # 创建资源文件
    set(RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    set(ICON_PATH "${RESOURCE_DIR}/Icons/logo.ico")

    # 确保图标文件存在
    if(EXISTS "${ICON_PATH}")
        # 将路径转换为Windows格式并正确转义
        file(TO_NATIVE_PATH "${ICON_PATH}" NATIVE_ICON_PATH)
        string(REPLACE "\\" "\\\\" ESCAPED_ICON_PATH "${NATIVE_ICON_PATH}")

        file(WRITE ${RC_FILE} "
#include <windows.h>
IDI_ICON1 ICON \"${ESCAPED_ICON_PATH}\"
")
        set(WIN_RESOURCES ${RC_FILE})
    else()
        message(WARNING "Icon file not found: ${ICON_PATH}")
    endif()
endif()

# 创建可执行文件
add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${TYPE_HEADERS}
    ${APP_HEADERS}
    ${APP_RESOURCES}
    ${WIN_RESOURCES}  # 添加Windows资源文件
)
# 在 add_executable 之后添加
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
# 设置包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # src目录
    ${CMAKE_CURRENT_SOURCE_DIR}/Types    # Types目录
    ${CMAKE_CURRENT_SOURCE_DIR}/Utils    # Utils目录
    ${CMAKE_CURRENT_SOURCE_DIR}/Cal      # Cal目录
)

# 链接Qt库
target_link_libraries(${PROJECT_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

# Windows特定设置
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()


# 在现有代码的最后添加
# 安装可执行文件
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# 安装资源文件（如果需要）
if(EXISTS "${RESOURCE_DIR}")
    install(DIRECTORY "${RESOURCE_DIR}/"
        DESTINATION share/${PROJECT_NAME}/resources
        COMPONENT Resources
    )
endif()
