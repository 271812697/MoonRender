# MoonRenderToy（UseQt）API 文档

> 基于仓库 `https://github.com/271812697/MoonRenderToy.git` 的当前代码（commit `702fe053`）整理。
> 本文覆盖四个 CMake 目标：`Moon`（Qt 应用）、`MoonRender`（渲染静态库）、`MoonGeometry`（OCC 几何静态库）、`MoonTracer`（路径追踪渲染器）。

---

## 1. 项目概览

MoonRenderToy 是一个基于 **Qt + OpenGL** 的实时渲染器/编辑器，同时包含：

- 基于 **OpenCASCADE（OCC）** 的参数化几何建模与拓扑命名；
- PBR、IBL、SSAO、透明（depth peel）、阴影、后期处理（Bloom/FXAA/Tonemapping/AutoExposure）等实时渲染特性；
- 基于 fragment shader 的 GPU 路径追踪（Moon 编辑器内）；
- 独立的离线路径追踪渲染器 **MoonTracer**（源自 Atrc / AirGuanZ），支持多种渲染算法与 JSON 场景描述。

### 1.1 模块结构

| 目录 | CMake 目标 | 类型 | 职责 |
| --- | --- | --- | --- |
| `Moon` | `Moon` | executable | Qt 编辑器主程序（建模、视口、属性面板） |
| `MoonRender` | `MoonRender` | static lib | 渲染引擎：ECS、场景系统、HAL、资源管理、后处理 |
| `MoonGeomerty` | `MoonGeometry` | static lib | OCC 几何建模：TopoShape、布尔运算、STEP/IGES 导入导出 |
| `MoonTracer` | `MoonTracer` | executable | 离线路径追踪渲染器（JSON 场景驱动） |
| `Extern` | 多个第三方目标 | static lib | glad、spdlog、stb_image、assimp、tinygltf、tinyxml2、tracy、eigen、boost |

### 1.2 构建

- CMake ≥ 3.8，C++20；
- Qt ≥ 5.11（Core/Gui/OpenGL/Svg/Widgets）；
- OpenCASCADE（Windows 下通过 `cmake/OpenCascadeWin.cmake` 配置）；
- Eigen 3.4、Boost（`Extern` 或系统路径）；
- 根 `CMakeLists.txt` 依次 `add_subdirectory(Extern Moon MoonRender MoonGeomerty MoonTracer)`。

---

## 2. MoonRender —— 渲染引擎 API

公共头文件位于 `MoonRender/include/`。主要命名空间：

| 命名空间 | 内容 |
| --- | --- |
| `Core::ECS` | Actor、AComponent 及全部组件 |
| `Core::SceneSystem` | Scene、SceneManager、BVH 服务 |
| `Core::Rendering` | SceneRenderer、可见性标志、后处理栈 |
| `Core::ResourceManagement` | 模型/纹理/着色器/材质资源管理器 |
| `Core::Resources` | 引擎层材质（Material） |
| `Core::Global` | ServiceLocator（服务定位） |
| `Core::API` | ISerializable、IInspectorItem |
| `Rendering::Context` | GPU Driver |
| `Rendering::HAL` | 硬件抽象层（Buffer/Texture/Shader/Framebuffer 等） |
| `Rendering::Entities` | Camera、Light、Drawable、Entity |
| `Rendering::Resources` | Mesh、Model、Texture、Shader 及加载器 |
| `Rendering::Settings` | 渲染设置枚举与结构 |
| `Maths` | FVector2/3/4、FMatrix3/4、FQuaternion、FTransform |
| `Tools` | 事件、IniFile、时钟、工具类 |

### 2.1 ECS：Actor 与组件

#### `Core::ECS::Actor`

场景中的实体，生命周期由 `Scene` 管理。主要接口：

```cpp
T& AddComponent<T>(Args&&...);          // 添加组件
bool RemoveComponent<T>();              // 移除组件
T* GetComponent<T>() const;             // 查询组件
std::vector<std::shared_ptr<AComponent>>& GetComponents();

void SetName/SetTag/SetActive(...);
void SetParent(Actor&); void DetachFromParent();
std::vector<Actor*>& GetChildren();

// 生命周期回调
void OnAwake(); void OnStart(); void OnEnable(); void OnDisable();
void OnDestroy(); void OnUpdate(float); void OnFixedUpdate(float); void OnLateUpdate(float);

// 事件
static Event<Actor&> CreatedEvent, DestroyedEvent;
Event<AComponent&> ComponentAddedEvent, ComponentRemovedEvent;
```

#### `Core::ECS::Components::AComponent`

组件基类，虚函数：

```cpp
virtual void OnAwake() {}
virtual void OnStart() {}
virtual void OnEnable() {}
virtual void OnDisable() {}
virtual void OnDestroy() {}
virtual void OnUpdate(float p_deltaTime) {}
virtual void OnFixedUpdate(float p_deltaTime) {}
virtual void OnLateUpdate(float p_deltaTime) {}
virtual std::string GetName() = 0;
```

#### 内置组件

| 组件 | 说明 |
| --- | --- |
| `CTransform` | 局部/世界位置、旋转、缩放；父子变换；`GetWorldMatrix()`、`GetLocalMatrix()` |
| `CCamera` | 相机：FOV、near/far、clear color、投影模式、视锥剔除开关 |
| `CLight`（基类） | 颜色、强度 |
| `CDirectionalLight` | 平行光：阴影开关、阴影区域大小、是否跟随相机 |
| `CPointLight` | 点光源 |
| `CSpotLight` | 聚光灯 |
| `CAmbientBoxLight` / `CAmbientSphereLight` | 环境光（Box/Sphere） |
| `CModelRenderer` | 挂接 `Rendering::Resources::Model*`，视锥剔除行为（网格包围盒/自定义包围球） |
| `CMaterialRenderer` | 材质列表（最多 `kMaxMaterialCount`），`FillWithMaterial`、`SetMaterialAtIndex` |
| `CPostProcessStack` | 后处理栈（Bloom/FXAA/Tonemapping/AutoExposure） |
| `CReflectionProbe` | 反射探针 |
| `CBatchMeshLine` / `CBatchMeshTriangle` | 批量线/三角形网格（用于几何离散化显示） |

### 2.2 场景系统

#### `Core::SceneSystem::Scene`

```cpp
Scene();
Actor& CreateActor();                               // 创建 Actor
Actor& CreateActor(const std::string& name, const std::string& tag = "");
void RemoveActor(Actor*);  void AddActor(Actor*);
bool DestroyActor(Actor&); bool DelayDestroyActor(Actor&);
void CollectGarbages();
Actor* FindActorByName(const std::string&) const;
Actor* FindActorByTag(const std::string&) const;
Actor* FindActorByID(int64_t) const;
CCamera* FindMainCamera() const;

void Play();  bool IsPlaying() const;
void Update(float); void FixedUpdate(float); void LateUpdate(float);

// 默认内容
void AddDefaultCamera(); void AddDefaultLights(); void AddDefaultReflections();
void AddDefaultPostProcessStack(); void AddDefaultSkysphere(); void AddDefaultAtmosphere();

// 拾取 / 求交
void BuildSceneBvh();
bool RayHit(const Rendering::Geometry::Ray&, HitRes&);
bool ClipRayHit(const Ray&, const Maths::FVector4& plane, HitRes&);
bool PointPick(const Maths::FMatrix4& viewPortMatrix, int x, int y, float tolerance, PointPickRes&);
```

`Scene` 实现 `Core::API::ISerializable`（tinyxml2 XML 序列化）。内部维护 Actor ID 映射、`FastAccessComponents` 快速索引和 BVH。

#### `Core::SceneSystem::SceneManager`

```cpp
SceneManager(const std::string& p_sceneRootFolder = "");
void Update();
void LoadAndPlayDelayed(const std::string& path, bool absolute = false);
void LoadEmptyScene(); void LoadDefaultScene();
bool LoadScene(const std::string& path, bool absolute = false);
bool LoadSceneFromMemory(tinyxml2::XMLDocument& doc);
void UnloadCurrentScene();
Scene* GetCurrentScene() const;

// 事件
Event<> SceneLoadEvent, SceneUnloadEvent;
Event<const std::string&> CurrentSceneSourcePathChangedEvent;
```

### 2.3 渲染

#### `Core::Rendering::SceneRenderer`

继承 `Rendering::Core::CompositeRenderer`，负责将 `Scene` 解析为可绘制对象并执行绘制：

```cpp
SceneRenderer(Rendering::Context::Driver& p_driver, bool p_stencilWrite = false);

void BeginFrame(const Rendering::Data::FrameDescriptor&);
SceneDrawablesDescriptor ParseScene(const SceneParsingInput&);
SceneFilteredDrawablesDescriptor FilterDrawables(const SceneDrawablesDescriptor&, const SceneDrawablesFilteringInput&);
void DrawModelWithSingleMaterial(PipelineState, Model&, Material&, const Maths::FMatrix4& modelMatrix);
void Resize(int width, int height);
```

`SceneFilteredDrawablesDescriptor` 按不透明、透明、线框、UI 分组（`DrawableMap`，支持前到后/后到前排序）。

#### 可见性标志 `Core::Rendering::EVisibilityFlags`

```cpp
enum class EVisibilityFlags : uint32_t {
    NONE = 0,
    GEOMETRY = 1 << 0,
    SHADOW   = 1 << 1,
    REFLECTION = 1 << 2,
    ALL = GEOMETRY | SHADOW | REFLECTION
};
```

#### 后处理 `Core::Rendering::PostProcess`

- `PostProcessStack`：栈式后处理管线；
- 效果基类 `AEffect`；
- 内置效果：`BloomEffect`、`FXAAEffect`、`TonemappingEffect`、`AutoExposureEffect`。

### 2.4 资源管理

`Core::ResourceManagement::AResourceManager<T>` 提供统一的资源生命周期：

```cpp
T* LoadResource(const std::string& path);
void UnloadResource(const std::string& path);
bool MoveResource(const std::string& from, const std::string& to);
void ReloadResource(const std::string& path);
T* RegisterResource(const std::string& path, T* instance);
void UnregisterResource(const std::string& path);
T* GetResource(const std::string& path) const;
```

具体管理器：

- `ModelManager`：从文件或内存（顶点/法线/UV/索引）创建 `Rendering::Resources::Model`；
- `TextureManager`、`ShaderManager`、`MaterialManager`：分别管理纹理、着色器与材质。

### 2.5 GPU Driver 与 HAL

#### `Rendering::Context::Driver`

OpenGL 命令入口：

```cpp
Driver(const Settings::DriverSettings&);       // debugMode + 默认 PSO
void SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void Clear(bool color, bool depth, bool stencil, const Maths::FVector4& color = Zero);
void Draw(PipelineState pso, const Resources::IMesh& mesh,
          EPrimitiveMode mode = TRIANGLES, uint32_t instances = 1);
PipelineState CreatePipelineState() const;
std::string_view GetVendor/GetHardware/GetVersion/GetShadingLanguageVersion() const;
void OnFrameCompleted();
```

#### `Rendering::HAL`

- `Backend`：当前仅 OpenGL，`using Backend = GLBackend`（由 `GRAPHICS_API_OPENGL` 决定）；
- 资源类型：`Buffer`、`VertexBuffer`、`IndexBuffer`、`VertexArray`、`ShaderStage`、`ShaderProgram`、`Texture`、`TextureHandle`、`Framebuffer`、`Renderbuffer`、`UniformBuffer`、`ShaderStorageBuffer`；
- 每个类型都有 `T*` 模板基类（`Rendering/HAL/Common/`）与 OpenGL 实现（`Rendering/HAL/OpenGL/`）。

### 2.6 实体与资源

#### `Rendering::Entities`

- `Camera`：FOV/Size/near/far、投影矩阵、视图矩阵、视锥、`WorldToScreen`、`ProjectionFitToSphere`；
- `Light`：颜色、强度、衰减（constant/linear/quadratic）、聚光 cutoff、阴影设置、`GenerateMatrix()`、`CalculateEffectRange()`；
- `Entity` / `Drawable`：渲染实体与可绘制对象。

#### `Rendering::Resources`

- `Mesh`、`IMesh`：网格数据（顶点属性、包围盒）；
- `Model`：由多个 mesh 组成；
- `Texture`、`Shader`；
- 加载器：`ModelLoader`（assimp）、`ShaderLoader`、`TextureLoader`；
- 解析器：`AssimpParser`、`IModelParser`。

### 2.7 Maths

`Maths` 命名空间提供：

- `FVector2` / `FVector3` / `FVector4`；
- `FMatrix3` / `FMatrix4`；
- `FQuaternion`（含 `Identity`）；
- `FTransform`（位置/旋转/缩放，父子级联）。

### 2.8 Tools

- `Tools::Eventing::Event<Args...>`：`AddListener` / `RemoveListener` / `Invoke`，返回 `ListenerID`；
- `Tools::Filesystem::IniFile`：INI 读写；
- `Tools::Time::Clock`、`Date`；
- `Tools::Utils`：`OptRef`、`PathParser`、`Random`、`SizeConverter`、`String`、`SystemCalls`、`CircularIterator`、`EnumMapper`、`ReferenceOrValue`。

---

## 3. MoonGeometry —— 几何建模 API

### 3.1 核心类 `Part::TopoShape`

OCC `TopoDS_Shape` 的封装，支持拓扑命名（TopoShapeMapper/ElementMap）与历史追踪。主要接口：

#### 布尔运算

```cpp
TopoDS_Shape fuse(TopoDS_Shape) const;                          // 并
TopoDS_Shape fuse(const std::vector<TopoDS_Shape>&, Standard_Real tol = -1.0) const;
TopoDS_Shape cut(TopoDS_Shape) const;                           // 差
TopoDS_Shape cut(const std::vector<TopoDS_Shape>&, Standard_Real tol = -1.0) const;
TopoDS_Shape common(TopoDS_Shape) const;                        // 交
TopoDS_Shape common(const std::vector<TopoDS_Shape>&, Standard_Real tol = -1.0) const;
TopoDS_Shape section(TopoDS_Shape, Standard_Boolean approximate = false) const; // 剖切交线
TopoDS_Shape generalFuse(...) const;
```

#### 导入导出

```cpp
void importStep(const char*);  void exportStep(const char*) const;
void importIges(const char*);  void exportIges(const char*) const;
void importBrep(const char*);  void exportBrep(const char*) const;
void exportStl(const char*, double deflection) const;
void read(const char*); void write(const char*) const;
```

#### 查询与判断

```cpp
TopoDS_Shape getSubShape(const char* Type, bool silent = false) const;
TopoShape getSubTopoShape(const char* Type, bool silent = false) const;
bool hasSubShape(const char* Type) const;
bool isNull() const;  bool isValid() const;  bool isEmpty() const;
bool isClosed() const; bool isPlanar(double tol = 1e-7) const;
Base::BoundBox3d getBoundBox() const;
```

#### 建模辅助

```cpp
void sewShape(double tolerance = 1e-6);
TopoDS_Shape makeShell(const TopoDS_Shape&) const;
TopoShape& makeCompound(...);
TopoDS_Shape makePipe(const TopoDS_Shape& profile) const;
TopoDS_Compound slices(const Base::Vector3d&, const std::vector<double>&) const;
```

### 3.2 布尔运算封装 `FCBRepAlgoAPI_*`

对 OCC `BRepAlgoAPI_*` 的包装，位于 `MoonGeomerty/` 根目录：

| 类 | 运算 |
| --- | --- |
| `FCBRepAlgoAPI_Fuse` | 并 |
| `FCBRepAlgoAPI_Cut` | 差 |
| `FCBRepAlgoAPI_Common` | 交 |
| `FCBRepAlgoAPI_Section` | 剖切 |
| `FCBRepAlgoAPI_BooleanOperation` | 基类 |

### 3.3 `Part::Geometry` 与几何曲线/曲面

`Part::Geometry`（继承 `Base::Persistence`）封装解析几何：

```cpp
static std::unique_ptr<Geometry> fromShape(const TopoDS_Shape&, bool silent = false);
virtual TopoDS_Shape toShape() const = 0;
virtual const Handle(Geom_Geometry)& handle() const = 0;
Geometry* copy() const; Geometry* clone() const;
boost::uuids::uuid getTag() const;
// 扩展属性
std::weak_ptr<const GeometryExtension> getExtension(const Base::Type&) const;
void setExtension(std::unique_ptr<GeometryExtension>&&);
```

配套：`Geometry2d`、`GeometryExtension`、`GeometryInit`（`Part::GeometryTypeInit()` 注册几何类型，供主程序启动时调用）。

### 3.4 `Part::Interface` —— STEP/IGES 写设置

静态接口，控制导出行为：

```cpp
enum class Assembly { Off, On, Auto };
enum class Unit { Millimeter, Meter, Inch };

static void writeStepAssembly(Assembly);
static Standard_CString writeStepScheme();
static bool writeStepUnit(Unit);
// IGES 头信息
static bool writeIgesHeaderAuthor(Standard_CString);
static bool writeIgesHeaderCompany(Standard_CString);
static int writeIgesBrepMode();
```

### 3.5 `Base` 命名空间

数学与基础工具（源自 FreeCAD Base）：

- 数学：`Vector3D`、`Matrix`（`Matrix4D`）、`Placement`、`Rotation`、`DualQuaternion`、`DualNumber`、`BoundBox`、`Precision`、`ViewProj`；
- 基础：`BaseClass`、`Type`、`Persistence`、`Handle`、`Exception`（`ValueError`、`CADKernelError` 等）、`Bitmask`、`FileInfo`、`Sequencer`、`TimeInfo`、`Tools`、`Tools2D`、`Converter`。

### 3.6 `App` 命名空间

- `ComplexGeoData`：几何数据基类（TopoShape 继承关系）；
- `FaceMaker`、`FaceMakerBullseye`、`FaceMakerCheese`：面生成器；
- `CrossSection`、`ExtrusionHelper`、`WireJoiner`、`modelRefine`：建模辅助；
- `OCCTProgressIndicator`：OCC 进度回调；
- `GizmoHelper`：Gizmo 辅助。

### 3.7 拓扑命名与缓存

- `TopoShapeMapper`、`TopoShapeCache`、`ElementMap`、`MappedName`、`IndexedName`、`StringHasher`、`ElementNamingUtils`、`TopoShapeOpCode`、`FuzzyHelper`：用于拓扑元素稳定命名、映射与历史追踪；
- `BRepMesh.h`：网格离散化辅助；
- `BRepOffsetAPI_MakeOffsetFix.h`：偏移修复。

---

## 4. MoonTracer —— 离线路径追踪渲染器 API

### 4.1 CLI 用法

```text
MoonTracer -d <scene.json>               # 从文件读取场景描述
MoonTracer -s "<json 文本>"              # 直接传入场景描述
MoonTracer -d <file> -s "<json 文本>"    # 以 -s 内容为准，-d 提供场景目录
MoonTracer -h                            # 帮助
```

参数定义位于 `cli/include/agz/cli/cli.h`：

```cpp
struct Params { std::string scene_description; std::string scene_filename; };
std::optional<Params> parse_opts(int argc, char* argv[]);
```

### 4.2 JSON 场景配置

JSON 通过 `agz::tracer::factory` 转换为 `Config` 树。顶层结构：

```json
{
  "scene": { ... },
  "rendering": { ... }
}
```

`rendering` 可以是单个 group 或 group 数组（多会话依次执行）。

#### `scene`（type = `default`）

```json
{
  "scene": {
    "type": "default",
    "entities": [ { "type": "geometric", ... } ],
    "env":       { "type": "ibl", ... },
    "aggregate": { "type": "native" }
  }
}
```

#### `rendering` 会话字段

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `width` / `height` | 是 | 输出分辨率 |
| `camera` | 是 | `pinhole` 或 `thin_lens`（见下） |
| `renderer` | 是 | 渲染算法（见 4.5） |
| `reporter` | 是 | `stdout` 或 `noout` |
| `film_filter` | 否 | `box` 或 `gaussian`，默认 box |
| `post_processors` | 否 | 数组：`aces` / `gamma` / `oidn_denoiser` / `resize` / `save_gbuffer_to_png` / `save_to_img` |
| `eps` | 否 | 数值 epsilon，默认 `3e-4` |

相机示例：

```json
{
  "type": "thin_lens",
  "pos": [0, 0, 5], "dst": [0, 0, 0], "up": [0, 1, 0],
  "fov": 45, "lens_radius": 0, "focal_distance": 1
}
```

`pinhole` 兼容旧写法（`pos/dst/up/width/dist`）。

### 4.3 Factory API（`agz::tracer::factory`）

```cpp
// 定义创建器
template<typename T> class Creator {
    virtual std::string name() const = 0;
    virtual RC<T> create(const ConfigGroup& params, CreatingContext& ctx) const = 0;
};

// 工厂注册/查询
template<typename T> class Factory {
    void add_creator(Box<Creator<T>>&&);
    const Creator<T>* get_creator(const std::string& name) const;
};

// 创建上下文：按 "type" 字段分发
class CreatingContext {
    const PathMapper* path_mapper;
    const ConfigGroup* reference_root;
    template<typename T> RC<T> create(const ConfigGroup& params, Args&&...);
};
```

- 每个 `Factory<T>` 自动注册 `reference` 创建器（`"name": ["a", "b"]` 引用 `scene` 中已定义对象）；
- `PathMapper` / `BasicPathMapper`：路径占位符 `${working-directory}`、`${scene-directory}`；
- 配置树：`ConfigNode` / `ConfigGroup` / `ConfigArray` / `ConfigValue`，`ConfigGroup` 提供 `child_int/child_real/child_str/child_vec2/3/4/child_spectrum/child_transform2/3` 与 `child_*_or` 默认值变体；
- JSON 转换：`string_to_json`、`json_to_config`、`config_to_json`、`json_to_string`（nlohmann::json）。

### 4.4 核心接口（`agz::tracer`）

| 接口 | 说明 |
| --- | --- |
| `Renderer` | 渲染算法：`render()`（阻塞）、`render_async` / `stop_async` / `wait_async` |
| `Camera` | 相机采样/求值（`CameraSampleWeResult` 等） |
| `Scene` | 场景：实体、灯光、环境光、aggregate、相机 |
| `Entity` / `Geometry` / `Material` | 实体、几何、材质（Disney/Metal/Glass/Phong 等） |
| `Light` / `EnvirLight` / `Medium` | 灯光、环境光、参与介质 |
| `Texture2D` / `Texture3D` | 纹理 |
| `FilmFilter` / `Sampler` | 像素滤波与采样器 |
| `PostProcessor` / `RendererInteractor` | 后处理与进度报告 |
| `Aggregate` | 加速结构（`native` / `bvh` / embree） |

渲染会话（`factory/utility/render_session.h`）：

```cpp
struct RenderSetting {
    int width, height; real eps;
    RC<Camera> camera; RC<FilmFilter> film_filter;
    RC<Renderer> renderer; RC<RendererInteractor> reporter;
    std::vector<RC<PostProcessor>> post_processors;
};
class RenderSession { void execute(); };
RenderSession create_render_session(RC<Scene>, const ConfigGroup&, factory::CreatingContext&);
```

### 4.5 内置创建器

| 工厂 | 注册的 type 名称 |
| --- | --- |
| `aggregate` | `bvh`、`native` |
| `camera` | `pinhole`、`thin_lens` |
| `entity` | `geometric` |
| `envir_light` | `ibl`、`native_sky` |
| `film_filter` | `box`、`gaussian` |
| `geometry` | `disk`、`double_sided`、`quad`、`sphere`、`transform_wrapper`、`triangle`、`triangle_bvh`、`triangle_bvh_noembree`、`triangle_bvh_embree`（USE_EMBREE） |
| `material` | `disney`、`dream_works_fabric`、`glass`、`ideal_black`、`ideal_diffuse`、`invisible_surface`、`metal`、`mirror`、`paper`、`phong` |
| `medium` | `heterogeneous`、`homogeneous`、`void` |
| `post_processor` | `aces`、`gamma`、`oidn_denoiser`（USE_OIDN）、`resize`、`save_gbuffer_to_png`、`save_to_img` |
| `renderer` | `ao`、`particle`、`pt`、`pssmlt_pt`、`restir`、`restir-gi`、`sppm`、`vol_bdpt` |
| `renderer_interactor` | `stdout`、`noout` |
| `scene` | `default` |
| `texture2d` | `checker_board`、`constant`、`hdr`、`image`、`solid_image` |
| `texture3d` | `constant`、`image3d` |

渲染算法实现位于 `tracer/src/render/`（path_tracing、bidir_path_tracing、photon_mapping、particle_tracing、pssmlt、direct_illum 等）。

---

## 5. Moon —— Qt 编辑器应用层 API

### 5.1 入口（`Moon/main.cpp`）

```cpp
QApplication app(argc, argv);
app.setStyle(new DarkStyle);
MOON::Log::Init();
Part::GeometryTypeInit();                 // 注册几何类型
MOON::System::JobSystem::OnInit(2);       // 初始化任务系统
MOON::Editor editor;
editor.show();
QApplication::exec();
MOON::System::JobSystem::Release();
MOON::Log::Shutdown();
```

### 5.2 核心服务

#### `MOON::Log`

```cpp
static void Init(); static void Shutdown();
static Log& intance(); static std::shared_ptr<spdlog::logger> GetLogger();
bool addOutput(LogOutput*); void logMessage(LogOutput::Level, ...);
```

日志宏：`CORE_INFO/WARN/DEBUG/TRACE/ERROR`（spdlog）与 `LOG_INFO/WARN/DEBUG/TRACE/ERROR`（带输出面板），`CORE_ASERT` 调试断言。

#### `MOON::System::JobSystem`

```cpp
void OnInit(uint32_t reservedThreads = 1); void Release();
uint32_t GetThreadCount();
void Execute(Context&, const Function<void(JobDispatchArgs)>& task);
void Dispatch(Context&, uint32_t jobCount, uint32_t groupSize, const Function<void(JobDispatchArgs)>&, size_t sharedMemory = 0);
bool IsBusy(const Context&); void Wait(const Context&);
```

#### `MOON::SelectionManager`

```cpp
static SelectionManager& instance();
void select(const std::vector<SelectID>&); void clearSelect();
void setPreselect(SelectID); SelectID getPreselect();
void setSelectMode(SelectMode);   // OverrideSelect / AddSelect
```

### 5.3 `MOON::Editor`

`Editor` 是 `QMainWindow` 的主窗口（pimpl，`EditorInternal`），包含菜单、工具条、视口、属性面板与场景树。

### 5.4 `Editor::Core::Context`

编辑器全局上下文，聚合引擎服务：

```cpp
class Context {
public:
    std::unique_ptr<Rendering::Context::Driver> driver;           // OpenGL 驱动
    std::unique_ptr<EditorResources> editorResources;
    Core::SceneSystem::SceneManager sceneManager;
    Core::ResourceManagement::ModelManager modelManager;
    Core::ResourceManagement::TextureManager textureManager;
    Core::ResourceManagement::ShaderManager shaderManager;
    Core::ResourceManagement::MaterialManager materialManager;
    Tools::Filesystem::IniFile projectSettings;
};
```

通过 `Core::Global::ServiceLocator` 访问（宏 `GetService(Type)`、`RegService(Type, T)`）。

### 5.5 `Editor::Panels::SceneView`

编辑器 3D 视口：

```cpp
void Update(float deltaTime); void InitFrame(); void DrawFrame();
Core::SceneSystem::Scene* GetScene();
void FitToScene(const Maths::FVector3& dir); void FitToSelectedActor(const Maths::FVector3& dir);
void LookAt(const Maths::FVector3& pivot, const Maths::FVector3& dir, float radius);
Maths::FVector2 worldToScreen(const Maths::FVector3& worldPos);
void BuildBvh();
void SetGizmoOperation(Core::EGizmoOperation);
bool MouseHit(Maths::FVector3& out); bool MouseClipHit(Maths::FVector3& out, const Maths::FVector4& clipPlane);
PickingRenderPass::PickingResult GetPickResult();
Rendering::Geometry::Ray GetMouseRay();
Actor* GetSelectedActor(); void SelectActor(Actor&); void UnselectActor();
```

### 5.6 几何相关组件

- `MOON::TopoActor`（`core/component/TopoShapeActor.h`）：带 `Part::TopoShape` 的 Actor，自动建立面/边子 Actor（`CBatchMeshTriangle` / `CBatchMeshLine`）；
- `Core::ECS::Components::CTopoShape`：拓扑形状组件，支持面/边拾取高亮（`hoverChild`、`selectChildFaces`）、`HighLightOption`（颜色/透明模式）、`discretizationShape()` 离散化；
- `CGeometryLine`：几何线组件。

### 5.7 特征建模系统（`Moon/feature/`）

```cpp
class Feature : public TopoActor {
    virtual bool execute();                 // 派生特征重写
    void setBaseFeature(Feature*); Feature* getBaseFeature();
    Part::TopoShape getBaseTopoShape();     // 取基础形状
    Part::TopoShape& getPreviewShape();
    void makeDone();
};

class FeatureBody {                         // 特征树容器（单例）
    static FeatureBody& instance();
    void addFeature(Feature*); bool removeFeature(Feature*);
    Feature* getLastBaseFeature(Feature*); bool setBaseFeatureFor(Feature*);
};
```

内置特征：

| 特征 | 说明 |
| --- | --- |
| `SketcherFeature` | 草图特征 |
| `ExtrudeFeature` | 拉伸（正向/反向/双向/对称、through-all、up-to-face、加/减） |
| `RevolveFeature` | 旋转 |
| `FilletFeature` | 圆角 |
| `ThicknessFeature` | 抽壳 |

### 5.8 草图系统（`Moon/Sketcher/`）

- `SketcherObj`、`SketcherObjManager`：草图对象与管理；
- `SketcheTool2D`：二维草图工具；
- `SketchePlane2D`：草图平面。

### 5.9 交互与渲染扩展（`Moon/Interactive/`、`Moon/renderer/`）

- `EventWidget`、`GizmoBehaviour`（`EGizmoOperation`：TRANSLATE/ROTATE/SCALE 等）；
- `Im2DRenderer` / `Im3DRenderer`：Immediate 模式 2D/3D 绘制（调试绘制）；
- `ViewData`：视口数据；
- 渲染扩展：`DebugSceneRenderer`、`CameraController`、`GizmoRenderFeature/Pass`、`GridRenderPass`、`OutlineRenderFeature`、`PathTraceRenderPass`、`PickingRenderPass`、`PointRenderPass`、`DebugModelRenderFeature`。

### 5.10 IO

```cpp
namespace MOON::IO { void ReadSTEP(const char* filePath, Core::SceneSystem::Scene* scene); }
```

STEP 模型导入到场景（基于 OCC 读取，`Moon/io/io_occ_step.h`）。

### 5.11 属性控件（`Moon/Widgets/`）

- 属性基类：`Property`、`PropertyComponent`、`PropertyQtWidgets`；
- 具体控件：`BoolProperty`、`SliderFloatProperty`、`SliderIntProperty`、`FVec3Property`、`FVec4Property`、`ColorPickerProperty`、`EnumProperty`、`TextureDrop`、`TextureProperty`、`ComboBox`、`CheckBox`、`NumberLineEdit`。

---

## 6. 序列化格式

场景、Actor、组件通过 `Core::API::ISerializable` 以 tinyxml2 XML 序列化：

```cpp
virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) = 0;
virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) = 0;
```

`SceneManager::LoadScene` / `LoadSceneFromMemory` 负责加载；MoonTracer 则使用独立的 JSON/`Config` 描述格式（见 4.2）。

---

## 7. 典型使用示例

### 7.1 创建场景与 Actor

```cpp
Core::SceneSystem::Scene scene;
scene.AddDefaultCamera();
scene.AddDefaultLights();

auto& actor = scene.CreateActor("Cube", "geometry");
auto& transform = actor.AddComponent<Core::ECS::Components::CTransform>();
auto& modelRenderer = actor.AddComponent<Core::ECS::Components::CModelRenderer>();
auto& materialRenderer = actor.AddComponent<Core::ECS::Components::CMaterialRenderer>();
```

### 7.2 通过 SceneManager 加载场景

```cpp
Core::SceneSystem::SceneManager sceneManager;
sceneManager.LoadScene("scene.xml");
while (true) sceneManager.Update();
```

### 7.3 TopoShape 布尔运算与导出

```cpp
Part::TopoShape box, cylinder;
TopoDS_Shape result = box.fuse(cylinder);      // 并
result = box.cut(cylinder);                    // 差
result = box.common(cylinder);                 // 交

Part::TopoShape shape; shape.setShape(result);
shape.exportStep("result.step");
shape.exportStl("result.stl", 0.1);
```

### 7.4 MoonTracer 最小场景

```json
{
  "scene": {
    "type": "default",
    "entities": [
      {
        "type": "geometric",
        "geometry": { "type": "sphere", "radius": 1 },
        "material": {
          "type": "ideal_diffuse",
          "albedo": { "type": "constant", "texel": [0.8, 0.2, 0.2] }
        }
      }
    ]
  },
  "rendering": {
    "width": 800, "height": 600,
    "camera": { "type": "thin_lens", "pos": [0, 0, 5], "dst": [0, 0, 0], "up": [0, 1, 0], "fov": 45 },
    "renderer": { "type": "pt", "spp": 64 },
    "reporter": { "type": "stdout" },
    "post_processors": [
      { "type": "aces" },
      { "type": "save_to_img", "filename": "out.png" }
    ]
  }
}
```

运行：`MoonTracer -d scene.json`。

---

## 8. 目录速查

| 路径 | 内容 |
| --- | --- |
| `MoonRender/include/Core/` | 引擎核心：ECS、场景、渲染、资源管理 |
| `MoonRender/include/Rendering/` | HAL、实体、资源、设置、后处理 |
| `MoonRender/include/Maths/` | 数学库 |
| `MoonGeomerty/` | TopoShape、Geometry、布尔封装、Base/App |
| `MoonTracer/cli/` | CLI 参数解析 |
| `MoonTracer/factory/` | JSON→Config、创建器工厂 |
| `MoonTracer/tracer/` | 渲染核心接口与算法实现 |
| `Moon/core/` | 日志、任务系统、选择管理、组件 |
| `Moon/editor/` | 编辑器面板、命令、输入 |
| `Moon/feature/`、`Moon/Sketcher/` | 参数化特征与草图 |
| `Moon/renderer/` | 编辑器视口与渲染扩展 |
| `Resource/Moon/Data/Engine/Shaders/` | `.shader` 着色器资源 |
