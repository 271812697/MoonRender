# 交接：把 HAL 资源改成"后端对象 + 前端持句柄"（以 buffer 开始）

状态：**已完成（两批，2026-11-24）**。基线提交 `b8071d7a`；下方步骤 1–3 已按图落地并验证
（单线程改前/改后、单线程 vs 渲染线程、Debug vs Release 都逐像素相同），落地记录见
`docs/RenderThreadPlan.md` §8.75（buffer）。第二批按同一形状把 framebuffer / renderbuffer /
vertex array / program 也做完了：HAL 里是槽里的后端对象，`Rendering::Data::{Framebuffer,
Renderbuffer, VertexArrayObject, Program}` 是只持句柄的前端，记录见 §8.76；改动留在工作区未提交。
读回也命令化了：`SetTargetReadBuffer` / `ReadPixelsToBuffer` 是命令，`ReadPixels` 是"记录 + 等"
（`Driver::FlushAndWait`），`MapRead` / `IsFenceSignaled` 是真正的同步 driver API；HZB 因此重新
打开，用 `ReadPixelsToBuffer` + `InsertFence` + `DownloadBuffer` 三步做异步读回，记录见 §8.77。
上一次方向错误的尝试留在 `Build/handover-buffer-attempt.patch`（只作参考，方向是错的，见下）。

## 目标分层（用户确认过的模型）

```
前端对象（新写）  ──持有──>  ResourceHandle<HwBufferObject>  ──指向──>  HAL 对象（= 后端对象，槽里）
  BufferObject                                                            TBuffer：封装 gl id / fence / size
```

- `Rendering::HAL::TBuffer`（及其 `HAL::Buffer`）**就是后端对象**：住在 `HandleAllocator` 的槽里，
  内部**不含 handle**，`GLBufferContext` 只放 GL 状态（`glId`、`fence`、`type`、`allocatedBytes`）。
- 前端另写一类对象（建议 `Rendering::Data::BufferObject`），它持有 `ResourceHandle<HwBufferObject>`，
  方法全部是"录命令"，不碰 GL。
- 这和本项目 `HAL::Texture` / `Resources::Texture` 的现有分工一致：`HAL::Texture` 是槽里的后端对象
  （构造函数 `glCreateTextures`），`Resources::Texture` 只持句柄。

> 注意：**Filament 的 `FBufferObject` 是反过来的**（前端对象持 `Handle<HwBufferObject>`，句柄指向后端
> `HwBufferObject`/`GLBufferObject`，见 `filament/src/details/BufferObject.h`）。本项目按用户要求采用
> "HAL 对象 = 后端对象"的命名，结构等价，但**不要**照抄 Filament 的 `FBufferObject` 落点。

## 基线状态（`b8071d7a` 上 buffer 是什么样）

- `MoonRender/include/Rendering/Backend/BackendResources.h`
  `HwBufferObject { uint32_t name; }`（只是一个名字记录，不是基类）；`Records::CreateBuffer(handle)`、
  `Records::BufferName(handle)`。
- `MoonRender/include/Rendering/HAL/OpenGL/GLBuffer.h`
  `GLBufferContext{ handle, type, allocatedBytes, fence }`，`using GLBuffer = TBuffer<OPENGL, GLBufferContext>`。
  **前端对象里放着 handle** —— 这就是要拆掉的东西。
- `MoonRender/src/Rendering/HAL/OpenGL/GLBuffer.cpp`
  全部是 `Rendering::HAL::GLBuffer::` 的特化：构造里 `Allocate<HwBufferObject>()` + `EnsureBuffer`；
  `Allocate/Upload/Bind` 录命令；`BindRange/Unbind/MapRead/InsertFence/IsFenceSignaled/ClearFence` 直接 GL。
- `MoonRender/src/Rendering/Backend/BackendResources.cpp`
  `CreateBuffer` 里 `glCreateBuffers` 后 `Construct<HwBufferObject>(handle, name)`；`DestroyBuffer` 取 `name` 后
  `Deallocate` + `DeferredDeleter::Push(glDeleteBuffers)`。
- `ResourceOps::EnsureBuffer(handle)` 里 **写死** `EBufferType::VERTEX` —— 一个真 bug：所有 uniform/SSBO/index
  buffer 在后端都被记成 VERTEX。改造时顺手把 type 传进去（`EnsureBuffer(handle, type)`）。

## 上次尝试（已丢弃）与它的教训

`Build/handover-buffer-attempt.patch` 里做了：`HwResource` 基类、`HwBufferObject : HwResource{type,size}`、
`Backend::GLBuffer : HwBufferObject{glId,fence}`、前端 `HAL::Buffer`（把 `GLBuffer` 名字让给后端）、
驱动里 `handle_cast<GLBuffer>` 解析、`EnsureBuffer(handle, type)`。

**方向错在**：它仍把 `TBuffer` 当前端（对象里持 handle），只是把 gl 状态挪进 `Backend::GLBuffer`。
正确的是反过来：`TBuffer` 自己就是后端对象，handle 从前端挪出去。
可以直接复用的部分：`EnsureBuffer` 传 type、驱动里用 `handle_cast` 而不是 `Records::BufferName`。

## 落地步骤（每步可编译、可跑）

### 1. `TBuffer` 变成槽里的后端对象

- `BackendResources.h`：
  ```cpp
  struct HwResource { virtual ~HwResource() = default; };
  struct HwBufferObject : HwResource {          // 槽里的基类
      Settings::EBufferType type = Settings::EBufferType::UNKNOWN;
      uint64_t size = 0;
  };
  ```
- `HAL/OpenGL/GLBuffer.h`：
  ```cpp
  struct GLBufferContext {                      // 不再有 handle
      Settings::EBufferType type;
      uint64_t allocatedBytes = 0;
      uint32_t glId = 0;
      void* fence = nullptr;
  };
  class TBuffer : public Rendering::Backend::HwBufferObject { ... };
  using Buffer = TBuffer<OPENGL, GLBufferContext>;
  ```
  `TBuffer` 的构造函数在槽里被调用（`Construct`），因此它做 `glCreateBuffers` 并把 `glId` 存进 `glBuffer`；
  参数用 `BackendConstructionTag`（照 `HAL::Texture` 的做法），避免前端路径再录一条 create 命令。
- `BackendResources.cpp`：
  ```cpp
  void Records::CreateBuffer(ResourceHandle<HwBufferObject> h, Settings::EBufferType type) {
      GetHandleAllocator().Construct<HAL::Buffer, HwBufferObject>(h, type, HAL::BackendConstructionTag{});
  }
  ```
  `BufferName` → `handle_cast<HAL::Buffer>(h)` 后返回 `glId`（或者干脆删掉，驱动直接用 `handle_cast`）。
- `DestroyBuffer`：取 `glId`/`fence`，`Deallocate(handle)`（析构里删 GL 也行，照 `HAL::Texture`），
  `DeferredDeleter` 只留给 fence/名字。

### 2. 新前端对象 `Data::BufferObject`

```cpp
class BufferObject {
public:
    explicit BufferObject(Settings::EBufferType p_type);        // Allocate 槽 + 录 CreateBuffer(type)
    ~BufferObject();                                            // Records::DestroyBuffer(handle)
    void Allocate(uint64_t p_size, Settings::EAccessSpecifier = STATIC_DRAW);
    void Upload(const void* p_data, std::optional<HAL::BufferMemoryRange> = {});
    void Bind(std::optional<uint32_t> p_index = {}) const;
    void BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const;
    void Unbind() const;
    void* MapRead(uint64_t p_offset, uint64_t p_size);          // 同步点，见下
    void Unmap();
    void InsertFence(); bool IsFenceSignaled() const; void ClearFence();
    uint64_t GetSize() const { return m_size; }
    bool IsValid() const { return static_cast<bool>(m_handle); }
    ResourceHandle<HwBufferObject> GetHandle() const { return m_handle; }
private:
    ResourceHandle<HwBufferObject> m_handle;
    Settings::EBufferType m_type;
    uint64_t m_size = 0;                                        // 录制侧要它定 upload/range 的大小
};
```

需要新增的命令（`DriverAPI.inc` + `OpenGLDriver` + `Driver` 的 Enqueue）：
`BindBufferRange{handleId,index,offset,size}`、`UnmapBuffer{handleId}`、`InsertFence{handleId}`、
`ClearFence{handleId}`。**`MapRead` 是同步 API**（要立刻拿 CPU 指针）：照 Filament `mapBuffer` 是
`DECL_DRIVER_API_RETURN`，单独处理；在那之前保留"require no driver 或 executor 线程"的断言 + TODO 注释。
`IsFenceSignaled` 同理需要一个同步查询（Filament `getFenceStatus` 也是 synchronous API）。

### 3. 调用点全换（`HAL::UniformBuffer`/`VertexBuffer`/`IndexBuffer`/`ShaderStorageBuffer` → `Data::BufferObject`）

- `MoonRender/src/Rendering/Resources/Mesh.cpp`（vertex/index buffer，`EnsureBuffer` 调用点也在这里）
- `MoonRender/src/Rendering/Data/UniformBufferPool.cpp`（`std::unique_ptr<HAL::UniformBuffer>`）
- `MoonRender/src/Core/Rendering/EngineBufferRenderFeature.cpp`（engine/reflection/object UBO）
- `MoonRender/src/Rendering/Overlay/Im2DRender.cpp`、`Im3DGLRenderer.cpp`、`Im3DType.cpp`
- `MoonRender/src/Core/Rendering/HzbBuildPass.cpp`（`std::shared_ptr<HAL::Buffer>`）
- `MoonRender/src/Rendering/Backend/BackendOpenGL` 相关：`PickingRenderPass.cpp`
- `Rendering/Data/Descriptor.*` 与 `MaterialInstance` 只存 `ResourceHandle<HwBufferObject>`，**不用改**；
  但它们内部的 `Records::BufferName` 要换成驱动侧 `handle_cast<HAL::Buffer>`。

## 验证

- 编译：`cmake --build Build --target Moon --config Release -- /nologo /m:1`（不要用 `/m`）。
- 两种模式各跑一次并截图，比较必须同尺寸、且**等 fit 动画跑完**：fit 是按帧数推进（598 帧），
  Debug/单线程帧率低，早截图两边不在同一帧，会误判成渲染差异。
  `QT_OPENGL=desktop`，`MOON_SHOT_DELAY_MS`、`MOON_SHOT`，`MOON_RENDER_THREAD=0` 为单线程。
- 对比脚本：`Build/refactor-shots/compare-dots.py A.png B.png`（CAD 红点双模态，判为 pass）。
- 注意工作区 EOL/BOM 约定：多数文件是 CRLF + UTF-8 BOM，改完用 `git diff --numstat` 确认没有整文件 EOL 变化。

## 其它未完成项（别忘）

- `Im2DRender::EndFrame` 开头 `m_programName == 0` 就整帧 return → 2D overlay（视图立方体/图标）不出现；
  需要把 program 名改成 handle（`SetProgramMatrix4Request` 已有 `programHandleId`）。
- `ImRenderer::newFrame` 每帧一次（否则 Debug 下 `!endFrameCalled` 断言）。
- 相机仍"不跟手"：见 `docs/RenderThreadPlan.md` §8.74 的排查清单（paste 隐式同步 / Flush 背压 /
  Debug 帧率 / 2D overlay 缺失 / 待加 `MOON_FRAME_TIMING`）。
- `ServiceLocator::Provide` 的 `IsProvided` 守卫 + 每帧 `std::cout << "type:"` 打印。
