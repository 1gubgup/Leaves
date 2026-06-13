# LeafField 地面落叶交互 Field

可拖入关卡的地面叶子交互插件。设计师把 `BP_LeafField` 拖到关卡并调用 `ActivateField()`，
一次性 Spawn 一批贴地静止的叶子；角色经过时叶子被实时"扇起"，落下后重新贴地静止。

> 更详细的实现说明见 [`Plugins/LeafField/ARCHITECTURE.md`](Plugins/LeafField/ARCHITECTURE.md)。

---

## 接入步骤

### 1. 启用插件
将 `Plugins/LeafField/` 整个目录拷贝至项目 `Plugins/` 下，重启编辑器并完成编译。

### 2. 主角挂载扰动源组件
打开主角的 Character / Pawn 蓝图 → `Add Component` → 添加 **LeafInteractionSource**。
默认参数即可，没有这个组件叶子不会被任何角色扰动。

### 3. 关卡中放置 Field
1. 把 `ALeafInteractionField`（或其蓝图子类 `BP_LeafField`）拖入关卡，放到铺叶子的位置。
2. 选中它，在 Details 面板设置：
   - **`LeafSystem`**：指向 `N_LeafField`（必填）。
   - **`LeafMeshes`**：添加 1~4 项，每项选一个叶片 `Static Mesh`，可选填 `Weight`（出现权重）。
   - 按需调整 `FieldExtent`（铺叶半径）、`LeafCount`（数量）、`Wind*`（风感）等。

### 4. 激活 Field
Field 默认处于 **Dormant**（不生成叶子）。需要由外部逻辑调用一次激活，例如在关卡蓝图
`BeginPlay`、GameMode 或剧情触发里：

```
Get Actor (BP_LeafField) → Activate Field
```

`ActivateField()` / `DeactivateField()` 均为蓝图可调用且幂等，可随时开关。

---

## 调整效果

### A. 单个 Field 的叶子与风感 —— `ALeafInteractionField`

选中关卡里的 Field Actor，在 Details 面板调整（每个 Field 互相独立）：

| 分类 | 参数 | 默认值 | 说明 |
|---|---|---|---|
| Asset | `LeafSystem` | — | Niagara System，指向 `N_LeafField`（必填） |
| Asset | `LeafMeshes` | — | 叶片网格列表（最多 4 种），每项 = Mesh + Weight。空槽权重自动锁 0、不参与随机 |
| Layout | `FieldExtent` | (500,500,10) | 铺叶半尺寸 cm，XY = 撒布半径，Z = 可视化框高度 |
| Appearance | `LeafCount` | 1024 | Spawn Burst 叶子数量 |
| Appearance | `GroundOffset` | 5 | 叶片贴地安全距离 cm |
| Wind | `WindStrength` | 1.0 | 本 Field 风强度倍率（0~5） |
| Wind | `WindLift` | 0.5 | 水平风转上抬力比例（0~1） |
| Wind | `WindNoiseScale` | 0.2 | 方向随机扰动强度（0~1） |
| Wind | `WindResponseMin/Max` | 0.05 / 0.1 | 叶子响应风力的最快/最慢时间（秒） |
| Wind | `WindSpinImpulse` | 1.0 | 被风踢起时的旋转冲量强度（0~10） |
| Advanced | `HeightCaptureZOffset` | 2000 | 高度相机在原点上方拍摄高度 cm |
| Advanced | `GroundBlendHeight` | 18 | 贴地过渡区高度 cm |

**多种叶子混撒**：在 `LeafMeshes` 里加多项即可。`Weight` 为整数比例、无需手动归一化，
例如三项权重 `[2, 1, 1]` → 出现概率 50% / 25% / 25%；留空的项默认权重 1，Mesh 空着的项权重锁 0。

### B. 单个角色的扰动范围 —— `LeafInteractionSource` 组件

选中挂载组件的角色，在组件 Details 面板调整：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `BrushRadiusUV` | 0.35 | 该角色扰动叶子的半径（UV 空间）；速度场 500cm 时约 175cm |
| `VelocityStrength` | 1.0 | 扰动强度倍率，BOSS 等大型单位可调至 2~3 |
| `VelocityDecayTime` | 0.25 | 停步后速度衰减时间常数（秒） |
| `bUsePeakHold` | true | 峰值保持（起步无延迟、停步柔和衰减） |

### C. 全局基准 —— 项目设置 → 插件 → Leaf Field

必须全局一致的参数（编解码端共享），在 `ULeafFieldSettings`：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `HeightRTSize` | 256 | 高度图 RT 分辨率（px），改后重启 PIE |
| `VelocityFieldWidth` | 500 | 速度场覆盖边长 cm（以本地 Pawn 为中心） |
| `VelocityFieldRTSize` | 128 | 速度场 RT 分辨率（px） |
| `WindMaxSpeed` | 500 | RG8 速度编码基准 cm/s，编解码端必须一致 |

---

## 故障排查

| 现象 | 检查项 |
|---|---|
| 叶子完全不出现 | ① 是否调用了 `ActivateField()`（默认 Dormant 不生成）<br>② `LeafSystem` 是否指向 `N_LeafField`<br>③ `LeafMeshes` 是否至少有一项填了有效 Mesh |
| 叶子出现但不被扰动 | ① 主角是否挂载了 `LeafInteractionSource` 组件<br>② 角色是否在速度场覆盖范围内（`VelocityFieldWidth`，默认 5m，以本地 Pawn 为中心） |
| 叶子穿地 / 悬空 | 调整 `GroundOffset`，并确认 `HeightCaptureZOffset` 高于地形（默认 20m） |
| 运行时改了地形叶子没跟上 | 地形默认只拍一次高度图；将 `bHeightCaptured` 重置为 false 后重新 `ActivateField()` |
| 人停下后叶子向固定方向飘 | `RT_VelocityField` 的 ClearColor 必须为 `(0.5, 0.5, 0, 1)`，若被误改请还原 |
| 叶子仅在世界原点附近响应 | 速度场以本地玩家 Pawn 为中心，确认场景存在合法的 PlayerController 与 Pawn |

---

## 资产清单

| 资产 | 用途 |
|---|---|
| `N_LeafField` | 叶子 Niagara 系统，由 Field 的 `LeafSystem` 引用 |
| `RT_VelocityField` | 速度场 RG8 渲染目标，C++ 自动加载 |
| `M_FluidSplat` | 速度笔刷材质，C++ 自动加载 |

`RT_VelocityField` 与 `M_FluidSplat` 的资产路径写死在代码中，不要重命名或移动。

---

## 工作原理

角色移动产生的速度被采集到一张以本地玩家为中心的速度场贴图（RG8）中，Niagara 粒子按自身
世界位置采样该贴图获得风速，从而被实时扇起；叶子下落时采样地形高度图对齐法线并贴地静止。
