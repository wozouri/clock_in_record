# 备份测试

此测试项目使用 Qt Core、Network、Sql 和 Test，需要 SQLite 驱动。
先构建客户端及 `AttendanceUpdateService`，然后在配置好 MSVC、Qt 的开发者命令行中执行：

```bat
cmake -S tests/backup -B out/build/backup-tests -G Ninja -DCMAKE_PREFIX_PATH="你的 Qt SDK 目录" -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/backup-tests
set ATTENDANCE_BACKUP_TEST_SERVICE=服务程序的绝对路径
ctest --test-dir out/build/backup-tests --output-on-failure
```

将对应 Qt SDK 的 `bin` 目录和服务程序依赖的 DLL 目录加入 `PATH`。测试会将服务程序复制到临时目录，以独立端口启动 HTTP 服务，不修改正式服务的配置或数据。

覆盖完整数据库快照及 SQLite 完整性检查、所有日期及备注/标记/工作制度的保留、空记录快照、三天保留与全局清理、当天备份更新、凭证持久化、路径穿越和大小限制、跨客户端 HTTP 拒绝、服务重启后的下载、开关首次上传、下载保存、阻止覆盖正在使用的数据库及启动补备份。通过可注入时钟跨越零点，验证自动上传最新快照。
