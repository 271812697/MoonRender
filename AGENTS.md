# 项目协作约定（MOON CAD）

## 提交

- **不要自动提交代码。** 改完把改动留在工作区，先向用户汇报改了哪些文件、为什么改、验证结果，由用户 review。
- **只有用户明确说"提交"（或同义指令）时才执行 `git add` / `git commit`。** 没听到这句话就不要动 git 的写操作。
- 同理，不要 `git push`、不要改写历史（`rebase` / `amend` / `reset`），除非用户点名要求。
- 需要暂存或提交时用 `git -c safe.directory="D:/Project/UseQt" ...`（仓库目录与当前用户不一致，不加会报 dubious ownership）。

## 验证

- 改了 C++ 就用真实编译验证，不要只靠阅读代码下结论：
  `cmake --build Build --target Moon --config Release -- /m /nologo`
  （增量编译需要访问 AppData 下的 Windows SDK，需在沙箱外运行。）
- 新增或移动源文件后先重跑 `cmake -S . -B Build` 重新生成工程。

## 接口

- 尽量保持既有公共接口不变。重构以"搬实现、改内部依赖"为主；确实要改签名时先说明理由并征求同意。
