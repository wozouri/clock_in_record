# Git 维护与提交建议

## 分支策略

- `main`: 稳定分支，只接收经过验证的合并。
- 功能/修复使用短生命周期分支：
  - `feat/<name>`
  - `fix/<name>`
  - `refactor/<name>`

## 提交信息格式

推荐格式：

```text
<type>(optional-scope): <subject>
```

要求：

- `subject` 使用祈使句，控制在 50 字符左右。
- 一次提交只做一件事，避免混合 `fix` 和 `refactor`。
- 需要时补充正文说明 why（动机）而不是重复 what（改动）。

## 常用 type

- `feat`: 新功能
- `fix`: 缺陷修复
- `docs`: 文档更新
- `style`: 仅格式调整
- `refactor`: 重构
- `perf`: 性能优化
- `test`: 测试相关
- `chore`: 工具链/杂项
- `build`: 构建系统变更
- `ci`: 持续集成配置
- `revert`: 回滚

## 提交拆分示例

```text
fix(calendar): fix departure time display format
refactor(calendar): remove context menu offset hacks
chore(main): guard Windows-only console setup
docs(readme): add build and commit conventions
```

## 合并前检查

- 本地可编译：`cmake --build out/build/x64-RelWithDebInfo --config RelWithDebInfo`
- 手工验证核心流程：新增记录、删除记录、导入/导出、月度统计
- `git status` 保持干净后再合并
