# 渲染线程分离 —— 交接文档

> **状态：已完成（2026-11-22 晚）。** 下面 §3 的 A–D 四项都已落地，最终形态与踩坑记录见
> `docs/RenderThreadPlan.md` §8.69–§8.72；改动留在工作区（未提交），验证结果为单/多线程
> 1954×812 逐像素相同（仅 CAD 拓扑红点的双稳态差异）。本节以下内容保留为原始任务说明。
>
> 写于 2026-11-22。上一轮会话在这里停下的原因：上下文耗尽，剩余工作明确但未完成。
> 目标一句话：**主线程只录命令，命令环装得下一帧，执行只发生在 `Execute()`；单线程与多线程的录制代码完全一致。**

---

## 1. 仓库状态

### 已提交（可直接用）

| commit | 内容 |
|---|---|
| `032381d4` | 渲染线程与主线程分离：录制/执行分离、叠加层命令化、灯光 SSBO 绑定修复 |
| `12f82532` | 相机跟手修复：呈现前用 GL fence 等这一帧真正画完（`WaitForFrameCompleted`） |
| `826bf4cc` | present 改成 3 槽环形缓冲（按 Qt/Filament demo 的交换链时序） |
| `4f3c1a6d` | 拾取修复：世界坐标附件改可变纹理（不可变存储无法 resize，1×1 会裁掉整个拾取目标） |
| `cbdef028` | IBL 绑定改执行期解析（去掉录制期 `IsValid()`） |

### 未提交（**已验证可用**，7 个文件，+31/−34）

| 文件 | 改动 |
|---|---|
| `MoonRender/src/Rendering/Context/Driver.cpp` | ① 命令环 `kRequiredCommandBytes` 8 KB→1 MB、`kCommandRingBytes` 64 KB→**8 MB**；② `Record()` 删掉"环满就执行"分支，只 `Enqueue`；③ `Driver::SetSamplerUnit` 改为纯入队（2 参数） |
| `MoonRender/include/Rendering/Backend/DriverAPI.inc` | `SetSamplerUnit` 命令签名 `(programId, name, unit)` → `(name, unit)` |
| `MoonRender/include/Rendering/Context/Driver.h` | 同上签名 |
| `MoonRender/src/Rendering/Backend/OpenGLDriver.cpp` | `OpenGLDriver::SetSamplerUnit(name, unit)`：对 `m_boundProgram` 取 location；`BindProgram` 记录 `m_boundProgram` |
| `MoonRender/include/Rendering/Backend/OpenGLDriver.h` | 新增 `uint32_t m_boundProgram = 0;` |
| `MoonRender/src/Rendering/Data/MaterialInstance.cpp` | 松散采样器分支删掉 `if (IsRenderThreadActive()) … else …`，两种模式只记 `BindDescriptor` + `SetSamplerUnit` |
| `MoonRender/src/Rendering/Overlay/Im2DRender.cpp` | 调用点同步 |

**验证结果**（`1954×812`，同一窗口尺寸）：

```
单线程 vs 线程模式 : 0.000%  逐像素相同
单线程 vs 基线     : 0.071%  （悬停/边缘抖动，与历史同量级）
构建               : 通过
崩溃               : 无
```

> ⚠️ 这批改动**尚未提交**。按 AGENTS.md，只有用户说"提交"才能 `git add/commit`。

---

## 2. 用户给出的设计规范（重要，别自己发挥）

1. **录制代码不分单线程/多线程**：两种模式主线程都只是把命令写进环形缓冲；单线程在帧尾手动 `execute()` 跑这一批。
2. **命令的执行逻辑里不允许出现录制**：执行器（`OpenGLDriver`）内部不得调用前端 API（`MaterialInstance::Bind`、`Descriptor::Bind`、`GLShaderProgram::Bind` 等）。
3. **`MaterialInstance` 只负责 PerMaterial 描述符集**：
   - `_BRDFLut`、`_IrradianceCube`、`_PrefilterCube` 在 **PerView** 描述符集，slot 是约定固定的，由 view 侧绑定（`ABaseRenderer::SetEnvironmentTextures` 已在做），**不归 MaterialInstance 管**；
   - `domainColorTex`、`colorLayer` 在 **PerMaterial** 描述符集，材质的 MatC 生成源码时已经带上声明，材质里是有参数的。
4. **贴图是"放进描述符集"**，不是 per-draw 现场 `BindDescriptor` + 告知 unit。绑定时通过 program 的 bindingMap（`DescriptorSet::Bind(program, kPerMaterialDescriptorSet)`）解析到固定 unit。
5. `_InputTexture` 目前是 **Blit 材质源码构建**的（`ABaseRenderer.cpp:148-164` 的 `CreateFromSource`），要改成 **MatC 方式**（`.shader` + `.mat`），并把 `_InputTexture` 声明为参数。
6. `m_executing` 这个变量**不应存在**（用户明确要求删掉）。它现在还在 `Driver.h` 里，仅被 frame path 那 6 处 `ExecutePendingCommands()` 用着。

---

## 3. 下一步任务（按顺序）

### 任务 A：Blit 材质改成 MatC（解掉松散采样器的最后一处）

新增两个资源文件（模板取自 `Resource\Moon\Data\Engine\Shaders\ClearBackground.shader`，全屏 quad）：

**`Resource\Moon\Data\Engine\Shaders\Blit.shader`**

```glsl
material {
    name         : Blit,
    shadingModel : unlit,
    blending     : opaque,
    culling      : none,
    depthWrite   : false,

    parameters : [
        { type : sampler2D, name : _InputTexture }
    ]
}

vertex {
    layout(location = 0) in vec2 geo_Pos;
    layout(location = 1) in vec2 geo_TexCoords;
    out vec2 TexCoords;
    void main()
    {
        TexCoords = geo_TexCoords;
        gl_Position = vec4(geo_Pos, 1.0, 1.0);
    }
}

fragment {
    in vec2 TexCoords;
    out vec4 FragColor;
    void main()
    {
        FragColor = texture(_InputTexture, TexCoords);
    }
}
```

**`Resource\Moon\Data\Engine\Materials\Blit.mat`**

```xml
<root>
    <shader>:Shaders\Blit.shader</shader>
    <settings>
        <support_orthographic>true</support_orthographic>
        <support_perspective>true</support_perspective>
        <blendable>false</blendable>
        <backface_culling>false</backface_culling>
        <frontface_culling>false</frontface_culling>
        <depth_test>false</depth_test>
        <depth_writing>false</depth_writing>
        <color_writing>true</color_writing>
        <cast_shadows>false</cast_shadows>
        <receive_shadows>false</receive_shadows>
        <captured_by_reflection_probes>false</captured_by_reflection_probes>
        <receive_reflections>false</receive_reflections>
        <user_interface>false</user_interface>
        <gpu_instances>1</gpu_instances>
        <draw_order>1000</draw_order>
        <polygon_offset>false</polygon_offset>
      </settings>
</root>
```

然后改 `ABaseRenderer.cpp:148-164`：删掉 `CreateFromSource(v, f)` 那段，改成从资源加载 `Blit.mat` 到 `m_presentMaterial`；绘制前 `m_presentMaterial.SetProperty("_InputTexture", 输入贴图)`。

> **待确认**：从资源加载 `.mat` 的 C++ 入口（`MaterialLoader` / `ResourceManager` 里的具体函数名）。这是上一轮卡住的地方——查一下 `Resources::Loaders` 目录，或者搜索已有材质是怎么加载的。新增资源文件后按 AGENTS.md 需要重跑 `cmake -S . -B Build`。

### 任务 B：删掉松散采样器路径

`MaterialInstance.cpp` 里对 sampler 的松散处理（`SetLooseUniform` 收到 sampler 名、`UploadProperties` 的按名绑定分支）整段删掉，`SetSamplerUnit` 命令与 `OpenGLDriver::SetSamplerUnit`/`m_boundProgram` 一并删除（任务 A 完成后 PerMaterial 采样器都走描述符集，PerView 的不归它管）。

### 任务 C：删除渲染路径里的同步执行（最终目标）

1. `Driver.cpp` 里这 6 处 `ExecutePendingCommands()` 删掉：
   `BindFramebuffer`、`SetViewport`、`SetScissor`、`Clear`、`Draw`、`SetPipelineState`；
2. 删掉 `Driver.h` 的 `m_executing` 与 `Driver.cpp` 里所有 `if (m_executing)` / `if (!m_executing)` 分支（录制桩里"就地执行"那条 return 分支）；
3. `ExecutePendingCommands` 只保留给"真的需要立刻拿结果"的地方：资源创建（`Link`、`Validate`、纹理上传）、读回（拾取、BVH）、`AView` 构造里初始化目标。

### 任务 D：剩下的 `IsRenderThreadActive()` 分流（HAL 前端）

共 17 处，上一轮改了 5 处（`Descriptor::Bind`/`Upload`、`GLShaderProgram::Bind`/`IsProgramRecorded`、`GLBuffer::Bind`、`MaterialInstance`），**都已回退**（未提交）。剩下要处理：

```
GLFramebuffer.cpp:152        GLRenderbuffer.cpp:16      GLShaderStage.cpp:18
ResourceOps.cpp:27           ShaderLoader.cpp:815       Im3DGLRenderer.cpp:159,207
AView.cpp:104,187,251,261,326
```

原则：前端函数**只入队**；执行器内部不得回头调前端。

---

## 4. 上一轮踩过的坑（别重复）

| 现象 | 真正原因 | 处置 |
|---|---|---|
| 删 drain 后画面**整体变暗** | `ABaseRenderer::SetEnvironmentTextures` 在**录制期**判 `p_irradianceCube->IsValid()`；天空捕获是命令，晚执行时 IsValid 为假 → `SetTexture` 被跳过 → 丢环境项 | 已修（`cbdef028`）：无条件记录，描述符存 handle，执行期解析 |
| 删 drain 后**面全丢**（只剩线框） | 单线程下 HAL 前端走"直接调 GL"分支，录制期就改了状态 | 未解决，见任务 D |
| 删 drain + 把绑定搬到录制侧后**整屏错但两模式逐像素相同** | 松散采样器路径在录制期取 `p_shader.GetID()`（此时 program 还没创建 = 0），命令里带的是 0 | 已修（未提交的那批）：改成 `SetSamplerUnit(name, unit)`，执行器用 `m_boundProgram` |
| 删 drain + 大环后**单线程崩 0xC0000005**（`glDrawElementsInstanced` 行） | 主线程读到 `m_executing == true`（普通 bool，跨线程读写），走"就地执行"分支但主线程没有上下文 | 曾用"原子 + 只让 driver 线程响应"修好；用户要求变量最终删除，**该状态未提交** |
| 单线程崩 `0xC0000005`（另一形态） | `Record()` 在环满时执行半帧，随后继续录制 → 环状态错乱 | 已修（未提交那批：环 8 MB + 纯追加） |
| 改了 3000 多行的假 diff | 脚本写文件时把 LF 改成了 CRLF | 见第 6 节"文件编辑纪律" |

**结论性事实**（已用代码/日志核实，不要再推翻）：

- `UpdateBufferRange` **已经**是"命令自带字节"（`Driver.cpp:229` 起，`shared_ptr<vector<uint8_t>>` 拷贝），不是共享 staging 指针；
- `QueryUniforms()` 在 `IsProgramRecorded()` 为真时直接 return（`GLShaderProgram.cpp:385`），注释写明"录制侧不许查反射"；
- 这个场景运行时出现的"松散 uniform"只有 6 个名字，且**全是采样器**：`_BRDFLut`、`_InputTexture`、`_IrradianceCube`、`_PrefilterCube`、`colorLayer`、`domainColorTex`；没有标量/向量松散 uniform；
- 打包材质（`.mat`）的参数块路径（`UploadParameterBlock`）是录制安全的：`SetTexture`(数据) + `UploadBuffer`(命令) + `StoreCPU`。

---

## 5. 验证流程（每步都做）

```powershell
cd D:\Project\UseQt
# 1) 构建（不要加 /m，沙箱下会静默 exit 1）
cmake --build Build --target Moon --config Release -- /nologo /m:1

# 2) 单线程截图
& taskkill /F /IM Moon.exe 2>$null
$env:QT_OPENGL="desktop"; $env:MOON_SHOT_DELAY_MS="13000"; $env:MOON_RENDER_THREAD="0"
$env:MOON_SHOT="Build\refactor-shots\X-single.png"
cmd /c ".\Build\bin\Release\Moon.exe `"C:\Users\27181\Desktop\ImGui-D3D11Hook-master\hezi.step`" > Build\refactor-shots\X-single.out 2> Build\refactor-shots\X-single.log"

# 3) 线程模式截图
Remove-Item Env:\MOON_RENDER_THREAD
$env:MOON_SHOT="Build\refactor-shots\X-thread.png"
cmd /c ".\Build\bin\Release\Moon.exe `"C:\Users\27181\Desktop\ImGui-D3D11Hook-master\hezi.step`" > Build\refactor-shots\X-thread.out 2> Build\refactor-shots\X-thread.log"

# 4) 像素对比
C:\Users\27181\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe `
  Build\refactor-shots\compare.py Build\refactor-shots\X-single.png Build\refactor-shots\X-thread.png
```

判据：

- 两模式 **0.000%** 或 ~0.07%（悬停抖动）→ 可接受；
- 有实体面（不是只剩线框）、亮度与历史基线一致；
- `GL_INVALID` 计数为 0（`Select-String -Path *.log -Pattern GL_INVALID`）；
- 崩溃则为 `exit code: -1073741819`（0xC0000005）等负数。

基线截图（同 `1954×812`）：`Build\refactor-shots\rH-single.png`、`rH-thread.png`（两者 0.000% 相同）。

---

## 6. 环境与文件编辑纪律

- 仓库：`D:\Project\UseQt`，git 必须带 `-c safe.directory="D:/Project/UseQt"`。
- 脚本：`run-single.bat`（设 `MOON_RENDER_THREAD=0`）、`run-thread.bat`（清掉它）。
- 现有诊断开关（都在，gated）：`MOON_FPS`、`MOON_FRAME_TRACE`、`MOON_DRAW_TRACE`、`MOON_PICK_TRACE`、`MOON_EXEC_TRACE`、`MOON_CMD_TRACE`、`MOON_PASS_PROBE`、`MOON_PICK_RADIUS`、`MOON_NO_PICK`、`MOON_PICK_SHOW`、`MOON_PICK_NO_SCISSOR`、`MOON_RENDER_MINIMAL`、`MOON_SHOT`、`MOON_UNIFORMS`、`MOON_NO_IBL`、`MOON_NO_BLOCK_COLOR`、`MOON_RENDER_THREAD`。
- **绝对不要用行号做编辑**，也不要整文件重写：
  - 用 `git show HEAD:<path>` 取原文，检测是否含 `\r\n`，改完按**原来的行尾**写回；
  - 否则会出现"3000 行假 diff"，并且历史上已经因此把文件内容改坏过、导致画面异常。
- 对比截图时窗口尺寸必须一致：应用会恢复上次窗口/相机状态，尺寸不同不能比。当前两台机都是 `1954×812`。
- 应用启动路径：`Build\bin\Release\Moon.exe`（Debug 在 `Build\bin\Debug\Moon.exe`）；模型：`C:\Users\27181\Desktop\ImGui-D3D11Hook-master\hezi.step`。

---

## 7. 相关文档

- `docs/RenderThreadPlan.md`：§8.64（BindProgramHandle 用 handle）、§8.67（灯光 SSBO 绑定）、§8.68（fence 等帧完成）、§8.70（拾取附件）、§8.71（IBL 执行期解析）；**§8.72 尚未写**（本轮"命令环按帧定尺寸 / 录制纯追加 / 采样器 unit 走执行期"应补写在这里）。
- `docs/ShaderPipeline.md`：MoonMatc 的 `.shader` 格式（任务 A 需要）。
