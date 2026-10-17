# 项目协作约定（MOON CAD）

## 提交

- **不要自动提交代码。** 改完把改动留在工作区，先向用户汇报改了哪些文件、为什么改、验证结果，由用户 review。
- **只有用户明确说"提交"（或同义指令）时才执行 `git add` / `git commit`。** 没听到这句话就不要动 git 的写操作。
- 同理，不要 `git push`、不要改写历史（`rebase` / `amend` / `reset`），除非用户点名要求。
- 需要暂存或提交时用 `git -c safe.directory="D:/Project/UseQt" ...`（仓库目录与当前用户不一致，不加会报 dubious ownership）。

## 验证

- 改了 C++ 就用真实编译验证，不要只靠阅读代码下结论：
  `cmake --build Build --target Moon --config Release -- /nologo /m:1`
- **不要加 `/m`**：MSBuild 的 `/m` 会起进程外工作节点，节点之间靠命名管道通信，沙箱禁止开
  命名管道，于是它一个字都不输出、直接 `exit 1`（日志里只剩一行 `Checking File Globs`，
  很容易误判成 CMake 出错）。`/m:1` 是单进程内并行，沙箱里可直接跑、不用提权；只有追求
  全量编译速度时才值得用完全权限 + `/m`。
- 新增或移动源文件后先重跑 `cmake -S . -B Build` 重新生成工程。

## 接口

- 尽量保持既有公共接口不变。重构以"搬实现、改内部依赖"为主；确实要改签名时先说明理由并征求同意。
