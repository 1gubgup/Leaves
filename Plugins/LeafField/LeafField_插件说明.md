# LeafField 落叶交互插件

## 一、功能概述

在地面铺设可与角色实时互动的落叶效果：

- 角色经过时，脚下叶子被扇起并吹散；
- 叶子下落时，自动贴合地形坡度并静止。

一块落叶区域对应一个 `LeafInteractionField` Actor，支持多块区域独立配置、同时运行。

**交互循环：**
```
角色移动 → 速度写入全局速度场 RT → 叶子采样 RT 获得风速 → 被吹起
叶子下落 → 采样地形高度图 RT → 对齐坡度法线 → 贴地静止
```

---

## 二、技术方案

| 技术点 | 用途 |
|--------|------|
| **Niagara GPU 粒子** | 承载所有叶子，单 Field 默认 1024 片，GPU 计算位置/姿态/速度 |
| **RenderTarget（速度场，RG8）** | 全局一张，记录角色移动产生的风速，叶子采样后被吹动 |
| **RenderTarget（高度图，R16f）** | 每个 Field 一张，朝下正交相机拍一次地形深度，叶子据此贴地 |
| **SceneCapture2D** | 拍摄高度图，仅首次激活时执行一次，地形视为静态 |
| **DrawMaterialToRenderTarget** | 使用 Splat 材质将角色速度绘制进速度场 RT |
| **WorldSubsystem** | 全局枢纽，每帧维护速度场并派发参数给所有激活的 Field |
| **DeveloperSettings** | 项目设置中的全局配置项 |

---

## 三、模块构成

### C++ 类

| 类 | 职责 |
|----|------|
| `ULeafFieldSettings` | 全局项目设置：速度场尺寸、RT 分辨率、风速编码基准 |
| `ULeafFieldSubsystem` | 数据流枢纽：每帧清空并绘制速度场，派发参数给所有激活的 Field |
| `ALeafInteractionField` | Field Actor：持有 Niagara 和高度相机，可拖入关卡 |
| `ULeafInteractionSourceComponent` | 扰动源组件：挂在 Actor 上，通过位置差分计算每帧速度 |

### 内容资产（路径硬编码，不可移动或重命名）

```
Content/LeafField/
├── RT_VelocityField   速度场渲染目标，RG8 格式，清屏色必须为 (0.5, 0.5, 0, 1)
├── M_FluidSplat       Splat 材质，参数：SplatCenterUV / SplatRadiusUV / SplatVelocity
└── N_LeafField        Niagara System，包含 Spawn、速度场、贴地三个 HLSL 模块
```

---

## 四、接入方式

**三步完成接入：**

1. **添加扰动源**：在角色 Blueprint 上添加 `LeafInteractionSourceComponent` 组件。
2. **放置 Field**：将 `LeafInteractionField` Actor 拖入关卡，`LeafSystem` 指向 `N_LeafField`，`LeafMeshes` 填入叶片网格，调整参数。
3. **（可选）全局配置**：项目设置 → 插件 → Leaf Field，调整速度场尺寸等全局参数。

### Blueprint 接口（`ALeafInteractionField`）

| 接口 | 说明 |
|------|------|
| `ActivateField()` | 激活 Field，生成粒子并开始每帧推参。幂等 |
| `DeactivateField()` | 停止粒子。幂等 |
| `RefreshParams()` | 运行时修改 Field 属性后调用，热推参数，无需重新激活 |

> 取消勾选 `bAutoActivateOnBeginPlay` 可禁用自动激活，改由关卡 Blueprint 或 GameMode 控制时机。

### 引擎模块依赖

```
Niagara / RenderCore / DeveloperSettings / Engine / CoreUObject
```

---

## 五、核心 HLSL 模块

> 以下代码与 `N_LeafField` 资产中的实际实现一致。

### 5.1 Spawn 模块（每次激活执行一次）

```hlsl
// 在 Field 矩形范围内随机散布（Z 置 0，由贴地模块每帧修正）
OutPosition = float3(
    FieldOrigin.x - FieldExtent.x + RandX * FieldExtent.x * 2.0,
    FieldOrigin.y - FieldExtent.y + RandY * FieldExtent.y * 2.0,
    0.0);

// 随机偏航角，平躺姿态
float hy = RandYaw * 3.14159;
OutOrientation = float4(0.0, 0.0, sin(hy), cos(hy));

// 按权重阈值选择 Mesh 槽位（0~3）
OutMeshIndex = (RandMesh < MeshThresholds.x) ? 0 :
               (RandMesh < MeshThresholds.y) ? 1 :
               (RandMesh < MeshThresholds.z) ? 2 : 3;
```

### 5.2 速度场模块（每帧）

```hlsl
// 世界坐标 → 速度场 UV，场外叶子不受力
float HalfW = VelocityFieldWidth * 0.5;
float2 velUV = float2(
    (Position.x - (VelocityFieldCenter.x - HalfW)) / VelocityFieldWidth,
    (Position.y - (VelocityFieldCenter.y - HalfW)) / VelocityFieldWidth);
if (!all(velUV >= 0.0 && velUV <= 1.0)) return;

// 解码风速（RG8，0.5 为零速中心）
float2 normVel = (velSample.rg - 0.5) * 2.0;
float3 windVel = float3(normVel * WindMaxSpeed * WindStrength, windMag * WindLift);

// 帧率无关一阶低通响应；Z 分量直接叠加
float Alpha    = saturate(DeltaTime * WindResponseSpeed);
OutVelocity.xy = lerp(InVelocity.xy, windVel.xy, Alpha);
OutVelocity.z  = InVelocity.z + windVel.z;

// 速度增量驱动翻滚冲量，使用 UniqueID hash 随机化旋转轴
OutRotationalVelocity = RotationalVelocity
    + float3(R1, R2, 0) * WindSpinImpulse * smoothstep(0.0, 100.0, speedDelta);
```

### 5.3 贴地模块（每帧）

```hlsl
// 高度图 UV（正交相机覆盖范围与 C++ 端 OrthoWidth = Max(X,Y)*2 对齐）
float captureSize = max(FieldExtent.x, FieldExtent.y) * 2.0;
float captureHalf = captureSize * 0.5;
float2 uv = clamp(float2(
    (Position.y - (FieldOrigin.y - captureHalf)) / captureSize,
    ((FieldOrigin.x + captureHalf) - Position.x) / captureSize
), 0.0, 1.0);

// 解码地面高度
float groundZ     = HeightCaptureZ - sc.r + GroundOffset;
float heightAbove = Position.z - groundZ;

// 仅在下落且近地时介入
float downWeight = step(0.0, -InVelocity.z);
float t = (1.0 - smoothstep(0.0, GroundBlendHeight, heightAbove)) * downWeight;

// 近地时对齐地形法线；防穿地并在落地后冻结速度与姿态
OutPosition = float3(Position.xy, max(Position.z, groundZ));
if (Position.z <= groundZ && InVelocity.z < 0) { /* 静止，姿态锁定贴地 */ }
```

---

## 六、参数说明

### 全局（项目设置 → 插件 → Leaf Field）

| 参数 | 默认 | 说明 |
|------|------|------|
| `HeightRTSize` | 256 | 高度图 RT 分辨率（px），越大贴地越精细，改后需重启 PIE |
| `VelocityFieldWidth` | 1000 cm | 速度场覆盖边长，以本地玩家为中心，改后需重启 PIE |
| `VelocityFieldRTSize` | 256 | 速度场 RT 分辨率（px），改后需重启 PIE |
| `WindMaxSpeed` | 1000 cm/s | RG8 编码基准；编解码两端必须一致，**勿随意修改** |

### Field Actor（Details 面板）

| 分组 | 参数 | 默认 | 说明 |
|------|------|------|------|
| Asset | `LeafSystem` | — | 指向 N_LeafField，必填 |
| Asset | `LeafMeshes[4]` | — | 叶片网格 + 出现权重，最多 4 种混撒 |
| Layout | `FieldExtent` | (500, 500, 10) cm | 铺设区域半尺寸（XY 为撒布半径） |
| Appearance | `LeafCount` | 1024 | 叶片数量，修改后需重新激活 Field |
| Appearance | `GroundOffset` | 5 cm | 叶片贴地安全距离 |
| Wind | `WindStrength` | 1.0 | 本 Field 风强倍率 |
| Wind | `WindLift` | 0.05 | 水平风速转垂直上抬力比例 |
| Wind | `WindResponseSpeed` | 5.0 | 叶子跟随风场的响应速度（帧率无关） |
| Wind | `WindSpinImpulse` | 1.0 | 被风吹起时的翻滚冲量强度 |
| Advanced | `HeightCaptureZOffset` | 2000 cm | 高度相机拍摄高度，须高于地形最高点 |
| Advanced | `GroundBlendHeight` | 10 cm | 贴地姿态过渡区高度 |

### 扰动源组件（`LeafInteractionSourceComponent`）

| 参数 | 默认 | 说明 |
|------|------|------|
| `BrushRadiusWorld` | 200 cm | 速度 Splat 笔刷半径 |
| `VelocityStrength` | 1.0 | 速度倍率，大型单位可适当调高 |
| `VelocityDecayTime` | 0.1 s | 停止移动后速度衰减时长 |
| `bUsePeakHold` | true | 峰值保持模式：起步零延迟，停步柔和衰减；false 为纯低通（起步有延迟） |

---

## 七、待确认事项

### 1. 叶子交互形态

已实现：**地面铺叶 + 角色经过被扇起 + 落回贴地**。

待确认是否还需要：

- [ ] 从树上 / 空中持续飘落（需新增 Spawn 逻辑与生命周期管理）；
- [ ] 其他形态（随风整体飘移、堆积等）。

### 2. 技术方案兼容性

- 两套 RT（高度图 + 速度场）的内存与带宽开销是否可接受；
- 高度图当前为**静态地形**方案，仅拍一次。若项目有运行时地形形变或可移动地面，需另行处理；
- 速度场默认以本地玩家为中心、边长 10 m。是否满足大场景或高速移动需求。

### 3. 扰动来源扩展

当前已支持角色移动和近战挥击（本质相同，传入笔刷位置 + 速度即可）。

待确认是否需要以下触发类型，以及由哪一侧提供调用接口：

- [ ] 爆炸：径向冲击，需提供爆心位置、半径、强度；
- [ ] 技能 / 挥砍：定向风，需提供位置、方向、强度；
- [ ] 其他（环境风、载具、法术 AOE 等）。

### 4. Field 生命周期管理

- 落叶区域是关卡预先布置，还是运行时动态生成 / 回收？
- 激活与销毁时机由项目侧控制，还是由插件自动管理？

---

## 八、目录结构

```
Plugins/LeafField/
├── LeafField.uplugin
├── Source/LeafField/
│   ├── Public/
│   │   ├── LeafFieldSettings.h               全局项目设置
│   │   ├── LeafFieldSubsystem.h              世界子系统
│   │   ├── LeafInteractionField.h            Field Actor
│   │   └── LeafInteractionSourceComponent.h  扰动源组件
│   └── Private/
└── LeafField_插件说明.md
```
