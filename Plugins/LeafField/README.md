# LeafField 地面落叶交互插件

可拖入关卡的地面叶子交互插件。把 `ALeafInteractionField` 拖到关卡，叶子自动铺满指定区域；角色经过时叶子被实时扇起，落下后重新贴地静止。

> 详细实现说明见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

---

## 接入步骤

### 1. 启用插件

将 `Plugins/LeafField/` 整个目录拷贝至项目 `Plugins/` 下，重启编辑器并完成编译。

### 2. 主角挂载扰动源组件

打开主角的 Character / Pawn 蓝图 → `Add Component` → 添加 **LeafInteractionSource**。
默认参数即可，没有这个组件叶子不会被任何角色扰动。

### 3. 关卡中放置 Field

1. 把 `ALeafInteractionField`（或其蓝图子类）拖入关卡，放到铺叶子的位置。
2. 选中它，在 Details 面板设置：
   - **`LeafSystem`**：指向 `N_LeafField`（必填）。
   - **`LeafMeshes`**：添加 1~4 项，每项选一个叶片 Static Mesh，可选填 `Weight`（出现权重）。
   - 按需调整 `FieldExtent`（铺叶半径）、`LeafCount`（数量）、`Wind*`（风感）等。

### 4. 激活 Field

`bAutoActivateOnBeginPlay`（默认 **true**）时 Field 会在 BeginPlay 自动激活，无需额外操作。

如需手动控制，取消勾选 `bAutoActivateOnBeginPlay`，然后在关卡蓝图 / GameMode 中调用：

```
Get Actor (ALeafInteractionField) → Activate Field
```

`ActivateField()` / `DeactivateField()` 均为蓝图可调用且幂等，可随时开关。

---

## 调整效果

### A. 单个 Field 的叶子与风感 —— `ALeafInteractionField`

选中关卡里的 Field Actor，在 Details 面板调整（每个 Field 互相独立）：

| 分类 | 参数 | 默认值 | 说明 |
|---|---|---|---|
| Asset | `LeafSystem` | — | Niagara System，指向 `N_LeafField`（必填） |
| Asset | `bAutoActivateOnBeginPlay` | true | BeginPlay 自动激活，无需外部蓝图调用 |
| Asset | `LeafMeshes` | — | 叶片网格列表（最多 4 种），每项 = Mesh + Weight。空槽权重自动锁 0 |
| Layout | `FieldExtent` | (500, 500, 10) cm | 铺叶半尺寸，XY = 撒布半径，Z = 可视化框高度 |
| Appearance | `LeafCount` | 1024 | Spawn Burst 叶子数量 |
| Appearance | `GroundOffset` | 5 cm | 叶片贴地安全距离 |
| Wind | `WindStrength` | 1.0 | 本 Field 风强度倍率（0~3） |
| Wind | `WindLift` | 0.1 | 水平风转上抬力比例（0~1） |
| Wind | `WindResponseSpeed` | 8.0 (1/s) | 叶子跟随风场的响应速度，帧率无关；越大越灵敏 |
| Wind | `WindSpinImpulse` | 0.5 | 被风踢起时的旋转冲量强度（0~5） |
| Advanced | `HeightCaptureZOffset` | 2000 cm | 高度相机在原点上方拍摄高度 |
| Advanced | `GroundBlendHeight` | 10 cm | 贴地过渡区高度 |

**WindResponseSpeed 参考值**

| 值 | 效果 |
|---|---|
| 1 | 约 1 秒响应，拖拽感强 |
| 8 | 约 0.12 秒响应（推荐默认） |
| 20 | 约 0.05 秒，几乎硬跟随 |

**多种叶子混撒**：在 `LeafMeshes` 里添加多项即可。`Weight` 为整数比例、无需手动归一化，
例如三项权重 `[2, 1, 1]` → 出现概率 50% / 25% / 25%；Mesh 空着的项权重锁 0、不参与随机。

### B. 单个角色的扰动范围 —— `LeafInteractionSource` 组件

选中挂载组件的角色，在组件 Details 面板调整：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `BrushRadiusUV` | 0.35 | 扰动半径（UV 空间）；速度场 500cm 时约 175cm |
| `VelocityStrength` | 1.0 | 扰动强度倍率，BOSS 等大型单位可调至 2~3 |
| `VelocityDecayTime` | 0.25 s | 停步后速度衰减时间常数 |
| `bUsePeakHold` | true | 峰值保持（起步无延迟、停步柔和衰减） |

### C. 全局基准 —— 项目设置 → 插件 → Leaf Field

必须全局一致的参数，在 `ULeafFieldSettings`：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `HeightRTSize` | 256 px | 高度图 RT 分辨率，改后重启 PIE |
| `VelocityFieldWidth` | 500 cm | 速度场覆盖边长（以本地 Pawn 为中心） |
| `VelocityFieldRTSize` | 128 px | 速度场 RT 分辨率，改后重启 PIE |
| `WindMaxSpeed` | 1000 cm/s | RG8 速度编码基准，编解码端必须一致 |

---

## 故障排查

| 现象 | 检查项 |
|---|---|
| 叶子完全不出现 | ① `LeafSystem` 是否指向 `N_LeafField`<br>② `LeafMeshes` 是否至少有一项填了有效 Mesh<br>③ 若关闭了 `bAutoActivateOnBeginPlay`，是否手动调用了 `ActivateField()` |
| 叶子出现但不被扰动 | ① 主角是否挂载了 `LeafInteractionSource` 组件<br>② 角色是否在速度场覆盖范围内（`VelocityFieldWidth` 默认 5m，以本地 Pawn 为中心） |
| 叶子穿地 / 悬空 | 调整 `GroundOffset`，并确认 `HeightCaptureZOffset` 高于场内最高地形（默认 20m） |
| 运行时改了地形叶子没跟上 | 地形默认只拍一次高度图；在蓝图或代码里将 `bHeightCaptured` 重置为 false 后重新 `ActivateField()` |
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

角色移动产生的速度被采集到一张以本地玩家为中心的速度场贴图（RG8）中，Niagara 粒子按自身世界位置采样该贴图获得风速驱动，叶子以帧率无关的一阶低通滤波跟随风场速度（由 `WindResponseSpeed` 控制响应灵敏度）；下落时采样地形高度图对齐法线并贴地静止。
