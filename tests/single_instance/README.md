# 单实例测试

此测试项目独立于客户端界面及更新服务，使用 Qt Core、Network 和 Test。
在配置好 MSVC 和 Qt 的开发者命令行中执行：

```bat
cmake -S tests/single_instance -B out/build/single-instance-tests -G Ninja -DCMAKE_PREFIX_PATH="你的 Qt SDK 目录" -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/single-instance-tests
ctest --test-dir out/build/single-instance-tests --output-on-failure
```

运行时需将对应 Qt SDK 的 `bin` 目录加入 `PATH`。

测试通过启动独立进程验证：重复启动的唤起通知、正常退出后重启、初始化期间并发启动、六个进程同时争锁、异常终止后恢复、数据目录隔离、无通信服务时的失败处理，以及无效数据目录。通信失败测试会等待超过 Qt 默认的 30 秒锁过期阈值，检查不会误删活进程的旧锁。整套测试约需 40 秒。

窗口最小化恢复、弹窗焦点和 Windows 任务栏提醒需要在桌面环境中验证。
