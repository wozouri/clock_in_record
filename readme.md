# 工时簿

一个基于 Qt Widgets 的本地考勤记录工具，支持按日期记录上下班时间、计算月度统计、导入导出 JSON。

## 功能

- 日历视图记录每日考勤
- 独立配置上下班、午休和晚餐休息制度
- 自动计算迟到/早退/加班和月度汇总
- 支持 JSON 导入与备份导出
- 本地存储，不依赖服务端

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
