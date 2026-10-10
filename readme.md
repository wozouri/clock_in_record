# 工时簿

一个基于 Qt Widgets 的本地考勤记录工具，支持按日期记录上下班时间、计算月度统计、导入导出 JSON。

## 功能

- 日历视图记录每日考勤
- 独立配置上下班、午休和晚餐休息制度
- 自动计算迟到/早退/加班和月度汇总
- 月度提示显示距离日均加班 2.0、2.5、3.0 小时的累计差值
- 每日独立作息、非工作日的标准时段加班开关和日历作息标记
- 删除和导入支持撤销/重做，导入前预览覆盖范围及工作制度影响
- 支持 JSON 导入与备份导出
- 本地存储，不依赖服务端
- 单实例运行，重复启动会唤起已有窗口
- 每天零点向局域网服务备份完整数据库，支持下载本机最近三天的备份

## 自动备份

在客户端设置的“本机备份”中开启自动备份，开关立即生效，首次开启会立即上传一次。客户端运行期间每天本机时间 00:00 备份；如果零点未运行、休眠或服务器暂时不可用，会在启动或恢复运行后补做当天备份，失败后每五分钟重试。备份使用设置中已保存的更新服务地址，需要同时升级服务端到支持备份的版本。

每份备份是完整 SQLite 数据库，包含所有日期的考勤记录、备注、统计标记和工作制度，数据为空时也会备份工作制度。服务端按自己的日期保留当天及前两天，每个客户端每天一份，手动“立即备份”会更新当天快照。单份文件上限为 20 MB。

服务端默认保存至服务程序目录下的 `backups/<客户端标识>/YYYY-MM-DD.db`。可在 `updateservice.ini` 的 `[service]` 段设置 `backupRoot` 为其他绝对路径。清理在服务启动及每分钟运行，即使客户端不再上传，过期文件也会移除；客户端身份凭证会保留。

“下载备份”只列出本客户端的备份，可选择日期保存 `.db` 文件；关闭自动备份后仍能下载已有备份。下载不会自动恢复或替换当前数据库，也不能保存到正在使用的考勤数据库路径。

客户端首次运行会读取可用的机器标识，与用户数据目录一起散列后生成稳定标识，再生成独立随机密钥；机器标识不可用时使用随机标识。IP 改变不会影响备份归属，同一机器上的不同 Windows 用户也互相隔离。服务器在首次上传时绑定凭证，以后所有上传、列表查询和下载都要匹配密钥，密钥不会放在 URL 中。原始机器标识不会发送到服务端。

请保留客户端数据目录中的 `backup.ini`，其中存有访问本人备份的标识和密钥，升级安装会继续使用。重装系统或丢失该文件后，不能只凭机器码找回访问权限，需要恢复原有 `backup.ini`。备份接口使用现有局域网 HTTP 服务。

备份集成测试的运行方法见 [备份测试](tests/backup/README.md)。

## 单实例运行

同一用户、同一数据目录只允许一个工时簿客户端运行。再次启动时，后启动的进程会通知已有窗口并退出；窗口最小化时恢复显示，存在编辑弹窗时优先唤起弹窗。Windows 限制前台切换时，通过任务栏闪烁提醒。

实例锁在打开数据库之前获取，正常退出会释放，异常终止后可重新启动。初始化或退出期间会短暂重试；如果无法联系已有实例，会显示提示并退出，不另开一个客户端。不同 Windows 用户使用各自的数据目录，更新服务作为独立程序运行。

多进程回归测试的运行方法见 [单实例测试](tests/single_instance/README.md)。

## 构建

环境要求：

- CMake >= 3.16
- Qt5 或 Qt6（Core、Widgets、Sql，需包含 SQLite 驱动）
- C++17 编译器

示例（Windows）：

```bash
cmake -S . -B out/build/x64-RelWithDebInfo
cmake --build out/build/x64-RelWithDebInfo --config RelWithDebInfo
```

## 数据存储与清理

本项目使用 SQLite 存储数据，不依赖服务端。

- Windows 数据库路径：`%APPDATA%\MyCompany\AttendanceApp\attendance.db`
- 首次运行 SQLite 版本时，会自动从旧注册表路径
  `HKEY_CURRENT_USER\Software\MyCompany\AttendanceApp` 导入考勤记录和工作制度；旧数据不会自动删除。
- 迁移或备份时，在程序关闭后复制 `attendance.db` 即可；也可以继续使用应用内 JSON 导出。
- 兼容已经发布的结构版本 5、6 数据库，读取时不降低结构版本，编辑或导入时保留旧版增加的字段。无法读取数据库时会提示错误并停止启动，避免显示空日历造成数据丢失的误解。

## 发布

构建目录会同时产出客户端 `AttendanceApp.exe` 与更新服务
`AttendanceUpdateService.exe`，两者是独立交付物。发布客户端时使用
`scripts/make_update_package.ps1`，脚本会：

- 使用 `windeployqt` 准备客户端与 Qt 运行时目录；
- 生成供客户端自动更新使用的 ZIP；
- 使用 Inno Setup 分别生成客户端与更新服务 Windows 安装程序；
- 写入更新服务读取的 `manifest.json`。

发布脚本先强制运行结构版本 4、5、6 的旧版数据库回归、未知版本 7 的错误提示测试，以及月度目标、旧版界面功能和完整备份回归。缺少测试程序、超时或任何测试失败都会停止发布，且不修改发布目录与版本清单。根项目默认启用 `BUILD_TESTING`，完整构建会生成 `storage_compatibility_tests.exe`、`stats_tests.exe`、`legacy_ui_tests.exe` 和 `backup_tests.exe`。传入 `-CheckOnly` 可仅检查发布条件，不生成安装包。界面回归在 Windows Qt 平台运行，备份回归使用独立临时数据库和隔离服务。

```powershell
powershell -ExecutionPolicy Bypass -File scripts\make_update_package.ps1 `
  -SourceDir out\build\vs2022-RelWithDebInfo\RelWithDebInfo `
  -Version v2026.10.10 `
  -UpdatesDir D:\AttendanceUpdates
```

需要将 `windeployqt.exe` 和 `ISCC.exe` 加入 `PATH`；也可以通过
`-WindeployQtPath`、`-IsccPath` 显式传入路径。产物分别位于
`packages`（自更新 ZIP）和 `installers`（Inno 安装程序）。下载服务网页会优先分发客户端安装程序，
客户端更新接口只使用 ZIP。

更新服务安装程序以管理员权限部署到 `C:\Program Files\AttendanceUpdateService` 并注册为
Windows 服务，默认监听 `0.0.0.0:47980`。
首次安装会在安装目录生成 `updateservice.ini`；升级不会覆盖该文件。安装完成后，
将客户端“更新服务”设置中的 IP 和端口指向部署机器即可。

## Git 提交规范

建议使用 Conventional Commits 风格，便于维护历史与生成变更日志：

- `feat`: 新功能
- `fix`: 缺陷修复
- `docs`: 文档更新
- `style`: 仅格式调整（不改语义）
- `refactor`: 重构（非新功能、非修复）
- `perf`: 性能优化
- `test`: 测试相关
- `chore`: 构建/依赖/工具链等杂项
- `build`: 构建系统或外部依赖变更
- `ci`: CI 配置变更
- `revert`: 回滚提交

详细示例见 `docs/git-workflow.md`。
