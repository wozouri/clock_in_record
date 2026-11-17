# 本地构建（Windows x64 Release）

优先使用 **x64 Release**，与仓库内 Windows x64 / Qt 5.15.2 / MSVC Release
预编译 Ela SDK 配套。预设仅提供 x64 Release，避免 Visual Studio 继续选中
未配置依赖的旧 Debug、x86 配置；需要其他架构或 Debug SDK 时先按
[Ela SDK 说明](third_party/ElaWidgetTools/README.md) 重建配套库。

## 依赖

- Visual Studio 的“使用 C++ 的桌面开发”组件及 Windows SDK。
- Qt 5.15.2 的 `msvc2019_64` 组件、CMake 3.21 或更新版本、Ninja。
- vcpkg 中的 `drogon:x64-windows`，缺少时执行
  `vcpkg install drogon:x64-windows`。

更新服务使用 Drogon，必须加载 vcpkg 工具链。只配置 Qt 路径时，
CMake 会在 `service/CMakeLists.txt` 报 `DrogonConfig.cmake` 找不到。

## 配置本机路径

将根目录的 `CMakeUserPresets.example.json` 复制为 `CMakeUserPresets.json`，
修改其中的 `QT_MSVC_ROOT` 和 `VCPKG_ROOT`：

```json
"environment": {
  "QT_MSVC_ROOT": "C:/Users/你的用户名/env/Qt/5.15.2/msvc2019_64",
  "VCPKG_ROOT": "C:/Users/你的用户名/env/vcpkg"
}
```

Qt 目录应包含 `bin`、`lib/cmake/Qt5` 和 `plugins`；vcpkg 目录应包含
`scripts/buildsystems/vcpkg.cmake` 以及已安装的依赖。
本机预设已加入 `.gitignore`，个人绝对路径不会提交到仓库。
也可以设置这两个环境变量后，直接使用共享的 `x64-release` 预设。
共享预设在缺少这些环境变量时不显示；本机预设读取自己的路径配置后才会显示，
避免误选未配置的预设。

## Visual Studio

使用“打开文件夹”打开项目根目录，选择 **本机 x64 Release**，
等待 CMake 生成完成后选择 `AttendanceApp.exe` 启动。

创建本机预设后，若配置列表尚未刷新，重新打开文件夹。
切换 Qt、工具链或架构时使用“删除缓存并重新配置”，避免旧缓存继续引用旧依赖。

## 命令行

在 Visual Studio 的 **x64 Native Tools** 开发者终端中进入项目根目录，
确保 CMake 和 Ninja 位于 `PATH`：

```bat
cmake --preset local-x64-release
cmake --build --preset local-x64-release --parallel 6
ctest --preset local-x64-release -R "^(theme_ui|monthly_overtime_targets)$"
```

构建和测试预设自动设置 Qt 运行库和插件路径。仅修改构建配置时按实际影响选择相关测试；
正式发布仍须执行发布脚本要求的完整检查，详见 [发布说明](readme.md#发布)。

客户端产物为 `out/build/local-x64-release/AttendanceApp.exe`，
更新服务为同目录下的 `AttendanceUpdateService.exe`。
从普通终端直接启动客户端时，需要将 Qt 的 `bin` 目录加入 `PATH`；
对外分发前使用发布脚本部署运行库。
