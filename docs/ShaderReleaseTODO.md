# 发布版 shader 相关记录

记录时间：2026-10-22。发布版不再携带 `Engine/Shaders` 文件夹，shader 在构建期离线编译并打进可执行文件（`Tools/ShaderPack` → `shader.cpp`），源码文本另以 `Source/<relative>` 条目一起打包。

## 已完成

- `ShaderPack` 为每个 `.shader` 同时打包：
  - `<relative>`：MoonMatc 编译出的 `.ovshader` 包，运行时 `ShaderLoader::Create` 用；
  - `Source/<relative>`：`.shader` 源码文本，编辑器用。
- `Rendering::Resources::ReadShaderSource` 优先查 `Source/<name>`，再回退到普通条目/磁盘。
- shader 编辑器、材质面板 Shader 下拉、内容浏览器 Shaders 根都从 `ShaderArchive` 列 `.shader`（跳过 `Source/` 前缀）；没有 Shaders 文件夹时内容浏览器用虚拟 `:Shaders` 节点。
- **MoonMatc 支持从 archive 解析 include**（2026-10-22）：
  - `Moon::Matc::CompileOptions` 新增 `IncludeResolver`（`std::function<bool(name, text, origin)>`）；
  - `ResolveInclude` 先问 resolver，失败再走磁盘 `includeDirs`；
  - 生成 stage 的公共 prelude（`Common/Buffers/EngineUBO.glsl`、`Common/Utils.glsl`）也走 resolver；
  - 编辑器 `MaterialEditorPanel::Apply` / 预览编译，以及 `MaterialDesigner::RecompileMaterialFeatures`（特性切换重编译）都接入基于 `ShaderArchive` 的 resolver；发布版没有 Shaders 文件夹时，源码和 include 都来自 archive。

## 剩余

- 做一次“删除 Shaders 文件夹的发布版”冒烟测试：
  1. 打开带 `#include` 的 `.shader`；
  2. 点 **Apply**，应能编译并热替换；
  3. 材质面板里切换一个 feature，应能重编译；
  4. 内容浏览器 Shaders 根应列出 archive 条目。
- 若以上都通过，本文件可以关闭。
