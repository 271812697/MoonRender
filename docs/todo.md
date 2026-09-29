# 文档补全记录

下列文档缺口已补齐：

## 草图约束文档

- [x] 约束求解后，参数和 GCS 对象如何更新回原来的几何 ——
  [SketchConstraints.md](SketchConstraints.md) §13/§14
  （`GeoDef`/`double*` 参数仓库/`param2geoelement`/`ConstrDef` 数据结构拆解，
  `applySolution → updateGeometry → extractGeometry` 回写链路与失败回滚）

## 草图绘制文档

- [x] 曲线绘制 —— [SketchRendering.md](SketchRendering.md) §2
  （离散化缓存、颜色/选中、构造虚线、自适应网格背景）
- [x] 标注绘制 —— [SketchRendering.md](SketchRendering.md) §3
  （约束图标锚点/样式/序号、尺寸与角度标注、标签拖动与拾取）

## 草图交互文档

- [x] 移动几何曲线（端点/边/圆心的不同编辑语义）——
  [SketchInteraction.md](SketchInteraction.md) §4
- [x] 几何曲线的拾取与吸附 —— [SketchInteraction.md](SketchInteraction.md) §1/§2

## 渲染

### 计划支持的功能

**剔除**

- [x] HZB 遮挡剔除（深度金字塔、上一帧深度、BVH 分层节点测试）——
  第一版已落地，原理与实现见 [HzbOcclusionCulling.md](HzbOcclusionCulling.md)；
  待办：视空间 bias、一帧延迟处理、统计可视化
- [x] 深度异步回读（PBO 环形缓冲，避免剔除引入同步 stall）——
  见 [HzbOcclusionCulling.md](HzbOcclusionCulling.md) §5.4
- [ ] 屏幕尺寸剔除
- [ ] LOD / 重要度预算（按屏幕占比 × 距离 × 语义重要度取舍）

**绘制提交**

- [ ] 64 位排序键（pass / pipeline / material / depth / mesh）
- [ ] 每对象数据入 SSBO（去掉逐 draw 的 uniform 上传与状态切换）
- [ ] 大量actor场景下，parsescene和filterdrawable的优化，开启遮挡剔除后，这两部分会占据比较长的时间
- [ ] 帧 Arena 分配（绘制列表、剔除结果不再逐帧 new/delete）

**HAL 能力**

- [ ] compute shader（新增 `COMPUTE` stage 与 dispatch）
- [ ] indirect draw / MDI（`DrawElementsIndirectCommand` + `glMultiDrawElementsIndirect`）
- [ ] stream compaction / prefix sum（生成紧凑可见列表与 indirect 参数）

**调试与可视化**

- [ ] 剔除统计与可视化（候选 / 视锥剔除 / HZB 剔除 / 实际 draw 计数）

### 其他

- [ ] 梳理一帧场景渲染的流程，以及可优化的方法
- [ ] 材质系统的重构

## 建模特征

### 变换特征（环形阵列 / 镜像 / 线性阵列）

三个特征都已落地（`PolarPatternFeature` / `MirrorFeature` / `LinearPatternFeature`），
默认 **feature 模式**（与 FreeCAD 一致）；原理见
[FeatureModeling.md](FeatureModeling.md)。

- [x] whole 模式（整份复制后 fuse）与 feature 模式（只重复特征自己的料）
- [ ] **多实体结果的处理**：feature 模式下副本挂在同一基底上，通常仍是单一实体；
  whole 模式下副本互不相交时会产生多个实体，目前整份保留
  （等价 FreeCAD 的 `AllowCompound = true`）。是否改成 FreeCAD 默认的
  "只取第一个 solid，其余放进 `rejected` 并给出一条警告"待定 ——
  注意改完只显示一个实例，需要同时给出提示或把 rejected 也画出来
- [ ] FreeCAD 的 `Spacings` / `SpacingPattern`（逐段间距、重复间距模式）未实现
- [ ] `ThicknessFeature` 还没有"自己的料"（`getToolShape()` 返回空），
  因此不能作为阵列/镜像的对象

## 草图

### 圆锥曲线的内部对齐几何

- [ ] 椭圆的内部对齐几何（中心点 + 长/短轴 + `InternalAlignment` 约束）。
  求解器已经支持 `InternalAlignment`（`Sketch.cpp` 的
  `buildInternalAlignmentGeometryMap`），但绘制 handler 目前只往草图里放一个
  `GeomEllipse`，没有任何内部元素。没有它，椭圆的尺寸
  （FreeCAD 的 `EllipseMajorDiameter` / `EllipseMinorDiameter`）无法标注，
  智能尺寸点椭圆时只能给出提示；B 样条同理（`Weight` 约束）

### 智能尺寸（SmartDimensionWidget）

- [x] 单元素尺寸（线长/水平/垂直、点到原点、圆直径、弧半径）、
  多元素尺寸（两点、点到线、圆到圆、两线角度）、连续标注、右键与 ESC 收尾
- [ ] 圆锥曲线尺寸 —— 依赖上面的"内部对齐几何"
- [ ] 可选：标完一个尺寸后是否自动释放鼠标。目前保持连续标注（与 FreeCAD 一致）；
  工具开启期间草图自身交互让路，已添加的标注文字需要关闭工具后才能拖动

## 拓扑命名

- [ ] `[TopoName]` 调试日志每个元素打一行，一次重算几十行；
  改为只在名字集合变化时输出，或降到 debug 级
- [ ] `DatumLineFeature` 写回的是裸 `TopoDS_Shape`，没有 element map
  （基准线目前没有名字）。现在没有按名字引用基准线的地方，
  如果以后要引用它的边，需要给它一个会建名字的入口
  （`makeElementCopy` 或带 op 的 maker）
