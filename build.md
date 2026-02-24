# Build 指南（Windows / Qt 5.15.2 + MSVC 2022）

本文档基于本机已验证环境整理，适用于当前项目 `AttendanceApp`。

## 环境路径

- Qt SDK: `C:\Qt\5.15.2\msvc2019_64`
- CMake: `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- Ninja: `C:\Qt\Tools\Ninja\ninja.exe`
- MSVC: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.43.34808`
- Windows SDK: `C:\Program Files (x86)\Windows Kits\10`

## 已验证构建命令

在项目根目录执行：

```bash
"C:/Qt/Tools/CMake_64/bin/cmake.exe" -S . -B "out/build/vs2022-RelWithDebInfo" -G "Visual Studio 17 2022" -A x64 -D CMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019_64"
"C:/Qt/Tools/CMake_64/bin/cmake.exe" --build "out/build/vs2022-RelWithDebInfo" --config RelWithDebInfo
```

构建产物：

- `out/build/vs2022-RelWithDebInfo/RelWithDebInfo/AttendanceApp.exe`

## 运行命令

```bash
"out/build/vs2022-RelWithDebInfo/RelWithDebInfo/AttendanceApp.exe"
```

## 说明

- 已在该环境完成配置与编译，并生成可执行文件。
- 启动程序时命令会持续运行（GUI 主循环），终端看起来会“卡住”是正常现象。
- 当前代码中部分历史文件存在编码告警（MSVC `C4828`），不影响本次构建产物生成。
