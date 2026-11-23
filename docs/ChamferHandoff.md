# Chamfer 迁移交接（viewport-driven modelling 第二个操作）

给下一个会话/同事：把 **Chamfer（倒角）** 接到已经落地的交互层里，和 Pad / Fillet 同一套模板。
按本文档做完即可构建、验证、提交，不需要重新设计。

基线：`f84c5b1c  Viewport-driven modelling: pad and fillet through an interaction layer`，
工作区干净。构建命令（**必须**，Release、`/m:1`）：

```
cmake --build Build --target Moon --config Release -- /nologo /m:1
```

新增源文件后先 `cmake -S . -B Build`（源文件是 `GLOB_RECURSE CONFIGURE_DEPENDS`）。

---

## 1. 已就位的架构（不要改）

| 文件 | 角色 |
|---|---|
| `Moon/editor/UI/TaskPanel/FeatureInteraction.h` | 抽象：`Attach`（只摆控件）/`Begin`（才建模）/`Commit`/`Abort`/`GetFeature`/`SetVisible`/`SetBeginCallback` |
| `PadInteraction`、`FilletInteraction` | 两个已迁移的操作，**照抄它们的结构** |
| `TaskSession` | `Idle→Offering→Active`，持有 `unique_ptr<FeatureInteraction>`，`Begin` 时用 `PanelFactory` 建面板并移交所有权 |
| `SelectionTaskLauncher` | 规则表 `kOfferRules`；多候选时在光标处弹 `QMenu`（`chooseRuleAtCursor`），按"边/面"记住上次选择（`m_lastEdgeRule`/`m_lastFaceRule`） |
| `ExtrudeTaskDialog`、`FilletTask` | 纯参数 UI，构造接 `unique_ptr<XxxInteraction>`，缺省 `MakeXxxPanel` 工厂 |

流程：点边 → `offerFillet`/`offerChamfer` 匹配 → `session.Offer(...)` 只摆箭头 →
拖箭头 → `Begin()` 建特征 + 预览 → `SetBeginCallback` → 面板接管同一 interaction。

## 2. 要做的五处改动

### 2.1 新文件 `Moon/editor/UI/TaskPanel/ChamferInteraction.h/.cpp`

以 `FilletInteraction.h/.cpp` 为模板，逐项替换：

| Fillet | Chamfer |
|---|---|
| `FilletFeature` | `ChamferFeature`（字段见 `Moon/feature/ChamferFeature.h`：`chamferType/size/size2/angle/flipDirection/useAllEdges/origin1..2/dir1..2/len`） |
| `radius` | `size`（第二尺寸 `size2`、角度 `angle` 只是参数） |
| 默认半径 `safeInitialFilletRadius(...)` | 主尺寸取 `diagonal * 0.03`（见旧 `ChamferTask.cpp:49`） |

必须保持的细节（都是从 Fillet 踩坑得来的）：

1. **Offer 阶段创建特征但不 execute**（`Attach`）：倒角箭头落点要靠 `getBaseTopoShape()` / `getBaseTopoEdgeShapes()` 解析，务必包 `try/catch`（`Standard_Failure`、`std::exception`），并 `setFeature()`。
2. **恒等值 = 0**：`Attach` 里 `size = 0`，箭头长度 0；低于 `m_minBuildRadius`（取 `len`）时**不 execute、不 preview**，保持原形状——否则 OCCT 会刷 `BRep_API: command not done`。
3. **`roundByCurrentSize()`**（照 `roundByCurrentRadius`）：`>= min` 才执行；失败就 `size *= 0.5` 最多 6 次，`execute()` 失败不破坏上一次有效结果，最后 `previewShape()`。
4. **两个箭头各一个回调**（`onHandle1Changed`/`onHandle2Changed` → `setSizeFrom(AxisTranslationWidget*)`），**不要**在回调里固定读 `m_handle1`（这是"只有一个箭头能拖"的根因）。
5. **`ensureHandles()` 里创建后立刻 `setActive(true)`**：只构造不启用 = 画得出来但收不到鼠标事件（另一个"拖不动"的根因）。
6. **`setUpScale(1.0f)`**：不要像旧面板那样传 `len`，那是世界尺寸缩放，会让箭头大小随模型变。
7. **`cot(θ/2)` 系数**：`m_radiusPerUnit = tan(acos(dir1·dir2)/2)`；拖动 `size = 箭头位移 * m_radiusPerUnit`，面板改值 `位移 = size / m_radiusPerUnit`。平面直边精确，曲边近似（与 FreeCAD 一致，属已知边界，不要再"修"）。
8. **观察者**：`ExecuteCommandPair m_lengthObserver1{0,nullptr}` 成员保存返回值；析构里 `handle->RemoveObserver(tag); delete pair.command;`（`AddObserver` 底层不会随节点释放 command）。
9. 预览样式：`mPreviewOption.isTransparent = false; isBlend = true; useDomainColor = false;`

### 2.2 `ChamferTask.h/.cpp` 改成纯面板

结构完全照 `FilletTask`（当前版本）：

- 头文件：`class ChamferTask : public ParamTaskDialog`（**去掉 `ShapeHelper`**）；
  构造 `ChamferTask(std::unique_ptr<ChamferInteraction> p_chamfer, QWidget* parent = nullptr)`；
  成员 `std::unique_ptr<ChamferInteraction> m_fillet;`+`Internal`（只放 UI 指针）；`Feature* feature() const;`
- 删掉 `Internal` 里的特征创建、两个 `AxisTranslationWidget` 创建、`~Internal` 里的 delete、`onWidgetLengthInvoke1/2`；
- `getParamValue`/`setParamValue` 改为经 interaction（尺寸）或 `feature()`（类型/角度/翻转/全部边）；
- `previewShape()` → `m_chamfer->UpdatePreview()`；`clickOk` → `Commit()`；`clickCancel` → `Abort()`；
- 边列表（`refreshReferenceRows`/`applyPickedEdges`/`removeSelectedEdges`）**原样保留**，只把 `mInternal->feature` 换成 `feature()`；
- 面板末尾注册 `m_chamfer->SetParamChangedCallback([this]{ refreshSize(); })`，刷新尺寸 slider；
- 文件末尾加 `BaseTaskDialog* MakeChamferPanel(std::unique_ptr<FeatureInteraction>)`（照 `MakeFilletPanel`）。

参数名以旧 `ChamferTask::getParamValue` 里的字符串为准（`"Chamfer:..."`），逐个对映，别改 UI 文案。

### 2.3 `SelectionTaskLauncher.cpp` 加一行

```cpp
{ "Chamfer", offersEdge, makeChamfer, &MakeChamferPanel },
```

`offersEdge` 现在叫 `offersFillet`（返回 `ShapeType() == TopAbs_EDGE`）——把它改名为 `offersEdge` 供两行共用；补 `makeChamfer()`。

加完**选边时菜单就会弹出**（Fillet / Chamfer），上次选择带勾选、按边记住。

### 2.4 `DesignModelingToolbar.cpp`

```cpp
if (name == "Chamfer") { return createChamferPanel(); }
```

`createChamferPanel()` 照 `createFilletPanel()`：`make_unique<ChamferInteraction>()` → `Begin()` → `new ChamferTask(std::move(...))`。

### 2.5 `TaskViewWidget.cpp`（编辑已有倒角）

`createFeatureDialog` 里 `ChamferFeature*` 分支：`make_unique<ChamferInteraction>(chamfer)` → `Begin()` → `new ChamferTask(std::move(...))`。

## 3. 验证清单

1. 选边 → 光标处出现 Fillet/Chamfer 菜单；选 Chamfer → 两个箭头（不执行、不预览）；
2. 拖任一箭头 → 面板出现，尺寸随拖动连续变大；另一箭头同步；
3. 低于初始尺寸时不 execute、日志无 `BRep_API`；
4. 面板改尺寸 → 箭头位置同步；Add/Remove/Clear 边 → 预览更新；
5. 点空白 → 箭头消失；再选同一条边 → 菜单再次出现且记住上次选择；
6. 平面直边下 `size` 与视觉一致；圆柱边为近似（已知）；
7. 菜单 "Chamfer"（先选中边）、树里编辑已有倒角 都可用；
8. **改动 `EventWidget`/`AbstractWidget` 这类基类的虚函数时，必须 `--clean-first` 全量重编**，否则新旧 vtable 混用会出现"控件看得见但拖不动"的假 bug。

## 4. 后续（本版不做）

- `Thickness` 迁移：面 + 单轴向控件，需要把 `ThicknessTaskDialog.cpp` 顶部那段 `getContinuousEdges` / `getDraggerPlacementFromEdgeAndFace` 搬进 interaction；迁移时 `setUpScale(1.0f)`。
- `EventObject::AddObserver` 根因泄漏（`SubjectHelper::RemoveObserver` 只删节点、不释放 command）：留待统一修；其余 5 个旧面板暂按现状。
- 旧面板残留的 `setUpScale(len)` 在各自迁移时一并去掉。

## 5. 提交

不要自动提交。做完后按项目约定：`git -c safe.directory="D:/Project/UseQt" commit -F <UTF-8 消息文件>`，
消息写清"Chamfer 走同一套 interaction 层 + 切换器实测"。
