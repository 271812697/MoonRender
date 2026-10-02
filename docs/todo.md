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

### 外部几何（参考导入的重构遗留）

外部几何的导入已经重做：`DrawSketchHandlerExternalGeometry` 负责拾取子形状与投影/求交，
草图只存投影后的曲线（`SketcherObj::addExternalGeometry`），坐标轴与原点成为外部块的固定尾部。
下面几条是重构评审时记下、暂时不修的：

- [ ] **引用是冻结的快照，参数化链接断了**：旧实现存 `(Feature*, reference, sourceShape)`，
  源特征变化时按 `dirty`/`updateExternalGeometry` 重投影；现在只存曲线，改上游特征
  （pad 长度、源草图）后外部曲线不会更新，任务面板也只能显示 `0  Line  [external]`，
  说不出它来自谁，可能出现"约束着一段已经和模型不符的几何"却无提示。
  最小补法：沿曲线再存一份 `(Feature*, reference, names)`，只用于"刷新"命令与面板文案，
  不做每次 solve 的重解析
- [ ] **轴末端是看不见的拾取/吸附点**：`ensureAxisGeometry` 给每条轴建了长度 500 的线段，
  而 `getCurveSegment` 对线段记录 start/end 两个 sepoint，于是 `(HAxis, end)=(500,0)`、
  `(VAxis, end)=(0,500)` 既能被拾取（`testSelect` 的 sepoint 循环），也是吸附目标
  （`snapPoint` 的 `snapToExternalPoints`，它在 `if (!ret)` 之前执行，会压过吸附到真实曲线），
  但那里什么都没画。建议采样后只留根点（`sepoints.resize(1)`），
  或在 sepoint 循环里滤掉 `isAxisCurve`
- [ ] `testSelect` 的 `originTole = 10.0` 是普通点容差 `deltaTole = 5.0` 的两倍，而且提前 `return`：
  原点周围 10px 一律先给原点，循环也只能离开原点、回不到原点
  （`findNextCoincidentPoint` 只扫 `mGeoList`）。建议对齐成 5
- [ ] `testSelect` 候选集上的注释说 "The drawing tools do not come through this function at all"，
  但 `DrawSketchHandlerLineSet::onButtonPressed` 确实调了 `testSelect`，
  真正兜住的是那句 `GeoId >= 0`。改掉措辞，免得以后新 handler 照这句注释写
- [ ] `mergeCirclePieces` 对 `loose.size() != 2` 一律返回整圆：
  只有"0 个散端"才推得出整圆，4 个散端是两段不相交的弧，现在会被换成整个圆。
  改成 `size()==0 → 圆 / size()!=2 → nullptr`
- [ ] `projectEdge` 每投一条边打一条 `CORE_INFO`（导入一个 12 条边的面就是 12 行），
  压成 debug，或只在投影不是 1:1 时打

### 几何类型判断的坑：`is<Part::GeomArcOfConic>()`

- [ ] `Part::GeomArcOfCircle` 调 `is<Part::GeomArcOfConic>()` 返回 **false**（但它对
  `is<Part::GeomArcOfCircle>()`、`isDerivedFrom<Part::GeomBoundedCurve>()` 都是 true）。
  于是 `getGeometryCenterSketch()` 里"先问 `GeomArcOfConic`、不行再问 `GeomBoundedCurve`"
  的顺序，会让圆弧落到有界曲线分支，返回**两端点的中点**（弦中点）而不是圆心 —— 半径标注
  的起点因此不在圆心，且与智能标注的预览对不上（预览用的是 sepoint 的 `mid`，那才是圆心）。
  现在那里已改成先按**具体类型**判断（`GeomArcOfCircle/Ellipse/Hyperbola/Parabola`）。
  根因未查：`TYPESYSTEM_SOURCE_ABSTRACT(Part::GeomArcOfConic, Part::GeomTrimmedCurve)`
  的注册，或 `Base::Type::isDerivedFrom()` 的祖先遍历有问题（抽象基类里只有它异常）。
  其它地方若再按抽象基类判断几何类型，会踩同样的坑

## 拓扑命名

- [ ] `[TopoName]` 调试日志每个元素打一行，一次重算几十行；
  改为只在名字集合变化时输出，或降到 debug 级
- [ ] `DatumLineFeature` 写回的是裸 `TopoDS_Shape`，没有 element map
  （基准线目前没有名字）。现在没有按名字引用基准线的地方，
  如果以后要引用它的边，需要给它一个会建名字的入口
  （`makeElementCopy` 或带 op 的 maker）

## 文档（`.moon` 序列化）

第一版已落地：`MoonDocument::save/open` + File 菜单的 Save / Save As / Open，
存的是"参数化特征链"（类型 + 参数 + 引用 + 草图），读取时按顺序重算整条链，
不存任何 B-Rep / 网格。下面这些是刻意的第一期取舍：

- [ ] **视图状态**完全没存：相机、材质、显隐、面板开关。第二期可以单独存一份
      "视口状态"（和模型分开，这样换机器看同一份模型时不会被别人的视角覆盖）
- [x] **UpToFace 的 pad/pocket**：面不再只当成一个 `TopoShape` 快照，
      改成 `upToFaceFeature + "Face_3" + 映射名` 的引用（`ExtrudeFeature::
      setUpToFaceReference`），`execute()` 每次重新解析，文件里存
      `<UpToFace feature="下标">`。剩下的取舍：
  - [ ] 选中面的引用只在**本 body** 的特征上有下标可写；面如果来自别的 body，
        写文件时仍然降级成普通长度（会 `CORE_WARN`）
  - [ ] 老文件（没有 `<UpToFace>` 元素）里的 UpToFace pad 仍然降级成普通长度
        （`CORE_WARN`），需要重新选一次面并保存
  - [x] **编辑已有特征时的 tip 回滚**（`ShapeHelper::rollBackToBase` /
        `restoreFeature`）：panel 打开时隐藏本特征、显示它下面的特征，确定时由
        `generateFinalShape()` 把本特征重新设为 tip，取消/析构时按打开前的显隐恢复。
        以前不做回滚，被编辑特征自己的形状（= 底座 + 自己加的料，底座的整张脸都在
        里面）是唯一能点到的，选到的面就会变成自引用。回滚后能点到的只有它下面的
        形状，引用自然落在正确的特征上。剩余取舍：
    - [ ] 回滚只处理**直接**的 base feature；链上更下面、但被别的分支隐藏的特征
          仍然看不见（正常建模路径用不到）
    - [ ] 选到 preview actor（不是任何特征的形状）的面时，靠"按名字去下面形状找
          同一个面 + 重心校验"兜底，找不到只 `CORE_WARN` 并保留本次会话的形状
- [ ] 草图的**标注标签手动位置**（`m_labelManualOffsetSketch` 等）没存，
      重新打开后标注回到默认位置（约束本身是存的）
- [ ] 每次加载，材质/模型管理器里按 `名字+ID` 注册的旧条目会**残留**（内存泄漏）。
      不影响显示（actor 直接持有指针），但反复加载会一直涨；要么给资源管理器加反注册，
      要么把资源改成按引用计数
- [x] **refine 挪到"结果被提交"的时刻**：原来只在 task panel 的"确定"
      （`ShapeHelper::generateFinalShape`）里 refine，读文件走 `execute()` 拿不到，
      于是保存前合并成一张的面读回来又裂开。现在放在 `Feature::makeDone()`
      （`refineResultShape()`），确定、整链重算、读文件这三条路都经过它；
      `execute()` 里不做，因为拖控件时每次鼠标移动都会 execute 一遍，而那时算出来的
      形状只是拿去显示。`Feature::isRefineActive()` 控制开关，`SketcherFeature` 返回
      false（草图是一根 wire，没有可合并的东西，名字还要被上层引用）。
      剩余取舍：
  - [ ] 拖控件时 preview 显示的是未 refine 的原始结果（省掉每次 refine），
        确定后才合面；要做成 FreeCAD 那样"预览也是 refine 过的"，得接受拖动时的
        refeine 开销（或者加节流）
  - [ ] 取消 panel 时会 `refineResultShape()` + `discretizationShape()` 一次，
        让留在模型里的形状（参数已经被改过）保持和提交过的一致
- [ ] 只有**单 body 单文档**（`FeatureBody` 是单例）；也还没有 New / 最近文件 /
      标题栏显示文件名 / 脏标记与关闭前询问 / 把读写接进撤销栈
- [ ] feature 之间的引用存的是**下标**（`base` / `profile` / `originals`）。
      第一期够用，但一旦支持"删除中间特征重排链"或"多文档互相引用"，
      就得换成稳定的对象 id（文件里再带一张 id → 下标的表即可）
- [ ] 文件只有 `version` 检查，没有**版本迁移**逻辑；改动格式时要顺手补一条
      "老版本怎么读"的路径，否则老文件会直接读不动
- [ ] `MoonDocument.cpp` 里的类型表是**手写的 dynamic_cast 链**（`typeNameOf` /
      `createFeature`），新增 feature 时容易忘；以后可以改成宏注册
