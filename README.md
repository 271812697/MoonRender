# Moon Render

基于 Qt + OpenGL 的 CAD 建模与渲染学习项目，核心是「草图建模 → 特征建模 → PBR/PathTrace 渲染」这条主线。
几何内核使用 OpenCascade（OCC），UI 基于 Qt Widgets，3D 预览与交互基于自研的交互 Widget 体系（Im3D 立即模式渲染）。

---

## 功能清单

### 几何建模（OCC）

- [x] 草图绘制（2D）：点 / 直线 / 多段线 / 圆 / 圆弧 / 椭圆 / 正多边形 / 腰形槽 / 圆弧槽 / 样条 / 矩形 / 旋转 / 对称 / 裁剪 / 圆角 / 偏移
- [x] 草图吸附、构造方式切换（M 键）、连续绘制模式
- [x] 草图约束求解：Coincident / H-V / Parallel / Perpendicular / Tangent / Equal / Symmetric / 尺寸（长度/半径/角度等），几何删除联动清理约束，拖拽走 GCS DogLeg
- [x] 特征建模（3D）：Pad（拉伸）/ Pocket（切除）/ Revolve / Groove / Thickness（抽壳）/ Fillet（圆角）/ Chamfer（倒角）
- [x] 特征预览：参数变化实时重建差异几何（半透明品红预览）
- [x] STEP / OBJ / GLTF 等模型导入

### 渲染

- [x] PBR + IBL（天空盒 / 辐照度 / 预过滤环境贴图）
- [x] PathTrace（GPU fragment）
- [ ] PathTrace（CPU / CUDA）
- [x] SSAO（深度感知，可开关）
- [x] 透明渲染（depth peel 深度剥离）
- [x] 剖切截面渲染（模板奇偶填充截面 + 几何着色器逐三角形求交截线，拾取同步按剖切面过滤）
- [x] 阴影（反射平面）
- [x] 后处理：Bloom / FXAA / Tonemap / Auto Exposure
- [x] HZB 遮挡剔除（上一帧深度金字塔 + BVH 分层节点测试，PBO 环形缓冲异步回读）
- [ ] LOD / 网格简化等性能优化

### 交互与选择

- [x] 交互 Widget：viewcube、剖切平面（ClipPlane）、平移/旋转手柄（AxisTranslationWidget 等）
- [x] 点选 / 框选 / 悬停高亮
- [x] 相机：环绕 / 平移 / 缩放 / 视角切换动画

### 编辑器 UI

- [x] 属性面板（Inviwo 风格折叠组、拖动数字控件）
- [x] 设置面板（折叠组分层：材质 / 调试 / 渲染 Pass）
- [x] 日志面板（Level / Time / Message 三栏，按级别过滤与着色）
- [x] 层级树（无线条、统一箭头）
- [x] 自定义 Dock 标题栏

---

## 架构文档

| 文档 | 内容 |
| --- | --- |
| [docs/InteractiveWidget.md](docs/InteractiveWidget.md) | 交互 Widget 体系：事件层 / 绘制层 / 拾取 / 状态机（ClipPlane 为例） |
| [docs/ScreenWidget.md](docs/ScreenWidget.md) | 2D 覆盖层交互控件架构：分层 / 坐标系统一 / 光标来源 / 状态机 / 光标归属仲裁 / 扩展步骤 |
| [docs/SketchModelingWidget.md](docs/SketchModelingWidget.md) | 草图建模 Handler 深入解析：事件 → 交互 → 曲线 → 预览 → 提交 |
| [docs/SketchModelingWidgetArchitecture.md](docs/SketchModelingWidgetArchitecture.md) | 草图建模架构总览（分层与依赖、设计模式） |
| [docs/SketchWidgets/README.md](docs/SketchWidgets/README.md) | 各草图工具专项文档（Point/Line/LineSet/Circle/Ellipse/Polygon/Slot/ArcSlot/BSpline/Rectangle/Fillet/Symmetry/Rotate/Offset/Trimming） |
| [docs/SketchConstraints.md](docs/SketchConstraints.md) | 草图约束：原理 / 架构 / 工作流 / 用法 / 一致性规则（求解器、删除清理、拖动锚点、setDatum） |
| [docs/SketchRendering.md](docs/SketchRendering.md) | 草图绘制：曲线 / 点 / 构造虚线 / 约束与尺寸标注 |
| [docs/SketchInteraction.md](docs/SketchInteraction.md) | 草图交互：拾取 / 吸附 / 几何拖动（端点、边、圆心的编辑语义） |
| [docs/FeatureModeling.md](docs/FeatureModeling.md) | Feature 参数化建模：预览逻辑 / 建模 / 任务 UI / 属性系统 |
| [docs/SectionRendering.md](docs/SectionRendering.md) | 剖切截面渲染（模板 / 奇偶裁剪） |
| [docs/HzbOcclusionCulling.md](docs/HzbOcclusionCulling.md) | HZB 遮挡剔除：深度金字塔 / BVH 分层测试 / PBO 异步回读 / 偏置与统计 |
| [docs/DevelopmentLog.md](docs/DevelopmentLog.md) | 开发工作记录：渲染（渲染效果 / 性能优化）与建模两大主题的工作归档 |
| [docs/ImguiArchitecture.md](docs/ImguiArchitecture.md) | ImGui 集成架构 |
| [docs/API.md](docs/API.md) | API 索引 |

---

## 构建说明

### 依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| Visual Studio | 2022（MSVC v143） | 编译器 |
| CMake | ≥ 3.12（建议 3.28） | 构建系统 |
| Qt | 5.15.2 `msvc2019_64` | Qt Widgets / Gui / OpenGL / Svg |
| OpenCASCADE | 7.9.3（x64，含 3rd-party DLL） | 几何内核 |
| Eigen / Boost | 3.4.0 / 项目自带 | `Extern/` 下 |
| spdlog / assimp / tinygltf 等 | 由 `Extern/` 子目录自动构建 | 无需手动安装 |

### 配置与编译

```bat
:: 1. 配置（OpenCASCADE_DIR 指向含 env.bat 的目录）
cmake -S . -B Build -DOpenCASCADE_DIR=D:/path/to/opencascade-7.9.3

:: 2. 构建（目标 Moon；Release）
cmake --build Build --target Moon --config Release -- /m /nologo

:: 3. 运行
Build\bin\Release\Moon.exe
```

> 提示：
> - 首次构建会自动编译 `Extern/` 下的 spdlog / assimp / tinygltf 等第三方库，耗时较长；
> - 运行时若提示缺少 OCC 或 3rd-party DLL，把 OpenCASCADE 的 bin 目录加入 `PATH`；
> - 编译前请关闭正在运行的 Moon.exe，否则链接会因文件占用失败。

---

## 截图

### OCC 几何建模

![image-20260906182824785](README.assets/image-20260906182824785.png)

![image-20260906182928007](README.assets/image-20260906182928007.png)

![image-20260906182959396](README.assets/image-20260906182959396.png)

![image-20260906183113472](README.assets/image-20260906183113472.png)



![image-20260906183144170](README.assets/image-20260906183144170.png)

![image-20260903150404609](README.assets/image-20260903150404609.png)

![image-20260902011211045](README.assets/image-20260902011211045.png)

![image-20260902011259652](README.assets/image-20260902011259652.png)

![image-20260902002025784](README.assets/image-20260902002025784.png)

![image-20260902001925245](README.assets/image-20260902001925245.png)

![image-20260902001844531](README.assets/image-20260902001844531.png)

![几何建模示例 1](README.assets/image-20260829160311142.png)
*草图绘制与 3D 特征建模效果*

![几何建模示例 2](README.assets/image-20260821131205204.png)
*特征建模：基于草图生成实体*

![几何建模示例 3](README.assets/image-20260521203930108.png)
*特征建模结果*

![几何建模示例 4](README.assets/image-20260822220525316.png)

### 透明（depth peel）

![透明渲染](README.assets/image-20260325225058918.png)
*深度剥离透明*



### 交互 Widget 架构

![交互 Widget 架构图](README.assets/image-20260819112630072.png)
*交互 Widget 体系架构*

需要补充：

1. 约束求解后，如何更新原来的几何的细节
2. 原来的几何和GCS对象，以及映射成参数的
