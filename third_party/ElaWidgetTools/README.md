# ElaWidgetTools SDK

本项目使用 Windows x64 / Qt 5.15.2 / MSVC Release 预编译 SDK；普通客户端编译无需引用外部项目。

2026-10-09 同步 SessionHub workspace 使用的 AppFusion 共享 Ela 源码，来源为
`AppFusion` 提交 `ca7a3dbe12840d2679d4adcb6228dfce4010112e` 下的
`third_party/ElaWidgetTools/ElaWidgetTools`（Ela 相关最近提交 `cc9729e`）。
搜索隐藏及动画优化由 `c3c8926` 引入：关闭搜索入口、隐藏用户卡片时，展开/收起不插入搜索动画占位，也不执行搜索按钮飞入动画。

在 MSVC 开发者终端中重新生成 SDK，`ELA_SOURCE_DIR` 填上述源码目录，`CMAKE_PREFIX_PATH` 填 Qt 5.15.2 MSVC 目录：

```bat
cmake -S third_party/ElaWidgetTools/rebuild -B out/build/ela-sdk -G Ninja -DELA_SOURCE_DIR=C:/path/to/AppFusion/third_party/ElaWidgetTools/ElaWidgetTools -DCMAKE_PREFIX_PATH=C:/path/to/Qt/5.15.2/msvc2019_64 -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/ela-sdk --target refresh_sdk
```

`refresh_sdk` 同步 DLL、导入库和全部公开头文件，三者必须配套更新。源码遵循同目录的 MIT [LICENSE](LICENSE)。
