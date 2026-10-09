# 旧版数据库兼容性测试

使用独立临时数据库验证结构版本 4、5、6 的记录读取、编辑、导入和完整备份；编辑不应清空旧版增加的统计标记与单日制度字段，结构版本不应降级。未知版本 7 必须返回明确错误，不能继续显示空日历。

```bat
cmake -S tests/storage_compatibility -B out/build/storage-compatibility -G Ninja -DCMAKE_PREFIX_PATH="你的 Qt SDK 目录" -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/storage-compatibility
ctest --test-dir out/build/storage-compatibility --output-on-failure
```

可对数据库快照运行 `storage_compatibility_tests --snapshot 快照路径`，它会复制到临时目录再测试，不修改原文件。

根项目默认启用 `BUILD_TESTING` 并加入这四个 CTest 用例。`scripts/make_update_package.ps1` 在写入任何发布文件之前强制执行同一构建目录的测试程序，缺少程序或测试失败都会终止发布。测试使用合成数据，不将个人数据库快照纳入仓库。
