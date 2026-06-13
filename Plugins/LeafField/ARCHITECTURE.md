# LeafField 插件架构文档

> 版本：1.0 | 引擎：Unreal Engine 5 | 模块类型：Runtime

---

## 一、插件概述

LeafField 是一个可拖入关卡的地面落叶交互插件。设计师把 `BP_LeafField`（继承自 `ALeafInteractionField`）拖到关卡，系统自动处理：

- **叶片初始化**：调用 `ActivateField()` 时，通过 Niagara Spawn Burst 生成贴地静止叶片
- **叶片扰动**：玩家移动时，速度被写入全局速度场 RT，驱动 Niagara 粒子被"扇起"
- **叶片贴地**：叶片落下时对齐地形法线并静止，起飞时自由翻转

---

## 二、目录结构

```
Plugins/LeafField/
├── LeafField.uplugin               插件描述（依赖 Niagara）
├── Source/LeafField/
│   ├── LeafField.Build.cs          模块构建规则
│   ├── Public/
│   │   ├── LeafFieldSettings.h     全局项目设置（UDeveloperSettings）
│   │   ├── LeafFieldSubsystem.h    世界子系统（注册中心 + 速度场）
│   │   ├── LeafInteractionField.h  Field Actor（一个可铺叶区域）
│   │   └── LeafInteractionSourceComponent.h  扰动源组件（挂在角色上）
│   └── Private/
│       ├── LeafFieldModule.cpp     模块入口
│       ├── LeafFieldSettings.cpp
│       ├── LeafFieldSubsystem.cpp
│       ├── LeafInteractionField.cpp
│       └── LeafInteractionSourceComponent.cpp
└── ARCHITECTURE.md                 本文档
```

---

## 三、核心模块

### 3.1 ULeafFieldSettings — 全局项目设置

**路径**：编辑器 → 项目设置 → 插件 → Leaf Field

存放必须全局一致的参数（编解码端共享）：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `HeightRTSize` | int32 | 256 | 高度图 RT 分辨率（px），改后重启 PIE |
| `VelocityFieldWidth` | float | 500 cm | 速度场覆盖边长（正方形） |
| `VelocityFieldRTSize` | int32 | 128 | 速度场 RT 分辨率（px），改后重启 PIE |
| `WindMaxSpeed` | float | 500 cm/s | RG8 编码基准，编解码端必须一致 |

每个 Field 独立的参数（`WindStrength`、`WindLift` 等）不在此处，在 `ALeafInteractionField` 上设置。

---

### 3.2 ULeafFieldSubsystem — 世界子系统

继承自 `UTickableWorldSubsystem`，每帧 Tick，是全插件的**数据流枢纽**。

#### 职责

1. **注册中心**：维护两张表
   - `Sources`：挂有 `ULeafInteractionSourceComponent` 的角色列表
   - `RegisteredFields` / `ActiveFields`：所有 Field / 当前激活的 Field

2. **全局速度场管线**（每帧，仅在 `ActiveFields.Num() > 0` 时运行）：
   ```
   ClearRT(ZeroVelocityColor)  →  SplatPass()  →  推参数给所有 ActiveFields
   ```

3. **VelocityFieldCenter 跟随**：每帧跟随本地 PlayerController 的 Pawn 位置（XY）

#### SplatPass 策略

当前采用"取最大速度"策略：N 个 Source 中选 XY 速度最快者，写入一次 Splat。

- 编码：`NormVel = Velocity / WindMaxSpeed`，存为 RG8（0.5 = 零速）
- Splat 材质路径：`/LeafField/LeafField/M_FluidSplat`
- 速度场 RT 路径：`/LeafField/LeafField/RT_VelocityField`（不存在则代码动态创建）

#### 接口

```cpp
void RegisterSource(ULeafInteractionSourceComponent*)
void UnregisterSource(ULeafInteractionSourceComponent*)
void RegisterField(ALeafInteractionField*)
void UnregisterField(ALeafInteractionField*)
void NotifyFieldActivated(ALeafInteractionField*)
void NotifyFieldDeactivated(ALeafInteractionField*)

UTextureRenderTarget2D* GetVelocityRT()
const FVector& GetVelocityFieldCenter()
float GetVelocityFieldWidth()
float GetWindMaxSpeed()
```

---

### 3.3 ALeafInteractionField — Field Actor

可拖入关卡的核心 Actor，代表"一块铺有落叶的区域"。

#### 组件树

```
SceneRoot (USceneComponent)
├── FieldBox         (UBoxComponent)              黄框：叶子铺设范围可视化
├── NiagaraComponent (UNiagaraComponent)          粒子系统
└── HeightCapture    (USceneCaptureComponent2D)   朝下正交深度相机
```

#### 状态机

```
Dormant  ──[ActivateField()]────▶  Active
Active   ──[DeactivateField()]──▶  Dormant
```

- **Dormant**：粒子未激活，Subsystem 不推参数
- **Active**：Subsystem 每帧调用 `PushDynamicParams`（仅推 `VelocityFieldCenter`）；静态参数在激活时由 `PushStaticParams` 推送一次

两个接口均为幂等：重复调用无副作用。激活/休眠由关卡蓝图、GameMode 或任意外部逻辑调用。

#### 高度捕获

- `HeightCapture`：朝下正交，`SCS_SceneDepth`，`ActivateField()` 首次调用时拍摄，之后复用
- 地形静态假设：`bHeightCaptured` 标记防止重复 SceneCapture 开销
- 若需强制重拍（运行时改地形），将 `bHeightCaptured = false` 重置即可

#### 可调参数

**Asset 类**

| 参数 | 说明 |
|------|------|
| `LeafSystem` | Niagara System 资产（必填，指向 N_LeafField） |
| `LeafMeshes` | 叶片网格列表（最多 4 种，每项 = Mesh + 出现权重）。空槽权重自动锁 0、不参与随机；推送到 `User.LeafMesh0~3` + `User.MeshThreshold0~3` |

**Layout 类**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `FieldExtent` | (500, 500, 10) cm | 叶子铺设半尺寸，XY = 撒布半径，Z = 可视化框高度 |

**Appearance 类**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `LeafCount` | 1024 | Spawn Burst 数量 |
| `GroundOffset` | 5 cm | 叶片贴地安全距离 |

**Wind 类（每个 Field 独立）**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `WindStrength` | 1.0 | 本 Field 风强度倍率（0~5） |
| `WindLift` | 0.5 | 水平风转上抬力比例（0~1） |
| `WindNoiseScale` | 0.2 | 方向随机扰动强度（0~1）；0 = 全部叶子方向一致，1 = 最大随机偏散 |
| `WindResponseMin` | 0.05 s | 最灵敏粒子的响应时间；建议 ≤ WindResponseMax |
| `WindResponseMax` | 0.1 s | 最迟钝粒子的响应时间；两值相等时所有粒子响应速度一致 |
| `WindSpinImpulse` | 1.0 | 叶子被风"踢起"时的旋转冲量强度（0~10）；0 = 不翻滚 |

**Advanced 类**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `HeightCaptureZOffset` | 2000 cm | 高度相机在 Actor 原点上方的高度 |
| `GroundBlendHeight` | 18 cm | 贴地过渡区高度（姿态对齐 + 旋转阻尼双重阈值） |

---

### 3.4 ULeafInteractionSourceComponent — 扰动源组件

挂在角色（或任何扰叶 Actor）身上的 `UActorComponent`。

#### 职责

- `BeginPlay`：向 `ULeafFieldSubsystem` 注册自己
- `TickComponent`：对 Owner 位置差分得出当前帧速度 XY，带峰值保持 + 低通平滑
- `EndPlay`：从 Subsystem 注销

#### 可调参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `BrushRadiusUV` | 0.35 | Splat 笔刷半径（UV 空间）；VelocityFieldWidth=500cm 时约 175cm 半径 |
| `VelocityStrength` | 1.0 | 速度倍率，调大叶子被扇得更猛 |
| `VelocityDecayTime` | 0.25 s | 停步后速度衰减时间常数 |
| `bUsePeakHold` | true | 峰值保持（起步无延迟、停步柔和衰减） |

---

## 四、整体数据流

```
[角色 Pawn]
    └── ULeafInteractionSourceComponent
          │  每帧位置差分 → CachedVelocityXY
          │  BeginPlay: RegisterSource(Subsystem)
          ▼
[ULeafFieldSubsystem]  (每帧 Tick)
    │  1. 跟随 Pawn → VelocityFieldCenter
    │  2. ClearRT(ZeroVelocity)
    │  3. SplatPass: 取最快 Source → DrawMaterial → VelocityRT (RG8)
    │  4. 遍历 ActiveFields → PushDynamicParams(Center)
    ▼
[ALeafInteractionField]
    │  PushDynamicParams: SetVariable VelocityFieldCenter（每帧，仅此一个）
    │  PushStaticParams (激活时/RefreshParams): SetVariable → NiagaraComponent
    │  (含全局静态: VelocityRT / Width / WindMaxSpeed)
    │  (含本地: HeightRT / HeightCaptureZ / FieldOrigin / Extent / GroundOffset / GroundBlendHeight)
    │  (含美术: LeafCount / LeafMesh0~3 / MeshThreshold0~3 / WindStrength / WindLift)
    │  (含风力: WindNoiseScale / WindResponseMin / WindResponseMax / WindSpinImpulse)
    ▼
[Niagara System: N_LeafField]
    ├── Init Module (Spawn Burst)
    │       世界坐标采样 HeightRT → 贴地初始位置
    │       随机 Yaw → 平躺初始姿态
    └── Update Module (每帧)
            ├── 速度场模块（VelocityRT → 风速 → lerp 驱动粒子速度）
            └── 贴地/碰撞模块（HeightRT → groundZ → 姿态/位置/旋转修正）
```

---

## 五、Niagara HLSL 模块说明

### 5.1 Init 模块 — 叶片初始化

**输入 Binding**

| 参数 | 类型 | 来源 |
|------|------|------|
| `RandX`, `RandY` | float [0,1] | Niagara Random Float |
| `RandYaw` | float [0,1] | Niagara Random Float |
| `HeightRT` | Texture2D | User.HeightRT |
| `HeightCaptureZ` | float | User.HeightCaptureZ |
| `FieldOrigin` | float3 | User.FieldOrigin（Actor 世界位置） |
| `FieldExtent` | float3 | User.FieldExtent（半尺寸） |
| `GroundOffset` | float | User.GroundOffset |
| `RandMesh` | float [0,1] | Niagara Random Float（多 Mesh 选择用，Spawn Only） |
| `MeshThreshold0~3` | float | User.MeshThreshold0~3（累积权重阈值，C++ 按 LeafMeshes 权重计算） |

**逻辑**

```hlsl
// 1. 随机世界坐标 XY（在 FieldExtent 范围内）
float worldX = (FieldOrigin.x - FieldExtent.x) + RandX * (FieldExtent.x * 2.0f);
float worldY = (FieldOrigin.y - FieldExtent.y) + RandY * (FieldExtent.y * 2.0f);

// 2. 采样高度图得到贴地 Z（UV 轴向：U=worldY, V=inv worldX）
float2 uv = float2(RandY, 1.0f - RandX);
float worldZ = HeightCaptureZ - HeightRT.SampleTexture2D(uv).r + GroundOffset;

// 3. 随机 Yaw，平躺姿态（绕 Z 轴旋转的四元数）
float yaw = RandYaw * 6.28318f;
OutOrientation = float4(0, 0, sin(yaw/2), cos(yaw/2));

// 4. 多 Mesh 选择：RandMesh 落在哪个累积阈值区间就用对应 Mesh slot
//    阈值由 C++ 按各 Mesh 权重归一化得出（如 [0.5, 0.8, 1.0, 1.0]）
int meshIdx = 3;
if      (RandMesh < MeshThreshold0) meshIdx = 0;
else if (RandMesh < MeshThreshold1) meshIdx = 1;
else if (RandMesh < MeshThreshold2) meshIdx = 2;
OutMeshIndex = meshIdx;   // → Particles.MeshIndex（绑定到 Mesh Renderer 的 Mesh Index Binding）
```

> **多 Mesh 渲染**：Mesh Renderer 配 4 个 slot 分别绑 `User.LeafMesh0~3`，`Mesh Index Binding` 设为 `Particles.MeshIndex`。C++ 端空槽用上一个有效 Mesh 占位（防 null），但其权重为 0、阈值不增长，永不会被选中。开销仅是按 MeshIndex 分批的 draw call（最多 4 个 instanced 批次），与单 Mesh 同量级。

---

### 5.2 Update 模块 — 速度场驱动

**输入 Binding**

| 参数 | 类型 | 来源 |
|------|------|------|
| `VelocityRT` | Texture2D | User.VelocityRT（RG8 编码） |
| `VelocityFieldCenter` | float3 | User.VelocityFieldCenter |
| `VelocityFieldWidth` | float | User.VelocityFieldWidth |
| `WindMaxSpeed` | float | User.WindMaxSpeed（解码基准） |
| `WindStrength` | float | User.WindStrength（本 Field 倍率） |
| `WindLift` | float | User.WindLift（水平转上抬比例） |
| `WindNoiseScale` | float | User.WindNoiseScale（方向随机扰动强度 0~1） |
| `WindResponseMin` | float | User.WindResponseMin（最快响应时间，秒） |
| `WindResponseMax` | float | User.WindResponseMax（最慢响应时间，秒） |
| `WindSpinImpulse` | float | User.WindSpinImpulse（起飞旋转冲量强度） |
| `UniqueID` | uint | 粒子唯一 ID（用于 hash 随机） |
| `DeltaTime` | float | 帧时间 |

**逻辑流程**

```
1. 世界坐标 → 速度场 UV
2. 范围外：保持原速度；范围内继续
3. 采样 VelocityRT，解码：NormVel = (rg - 0.5) × 2
4. 计算目标风速：windVel = normalize(normVel) × WindMaxSpeed × WindStrength
                          + Z 分量：windMag × WindLift
5. 整数 hash（基于 UniqueID）生成 per-particle 随机数
   - R1, R2：方向噪声扰动（± windMag × WindNoiseScale）
   - R3：个体响应时间（WindResponseMin ~ WindResponseMax，lerp Alpha）
6. OutVelocity = lerp(InVelocity × exp(-Drag×DT), windVel, Alpha)
7. 速度增量触发旋转冲量：
   speedDelta = max(0, |OutVelocity| - |InVelocity|)
   kickRatio  = smoothstep(0, 200 cm/s, speedDelta)
   OutRotationalVelocity = RotationalVelocity + float3(R1, R2, R1+R2) × WindSpinImpulse × kickRatio
```

**关键设计点**：用整数 hash 替代 sin 随机，分布更均匀、GPU 更友好。

---

### 5.3 Update 模块 — 贴地/碰撞处理

**输入 Binding**

| 参数 | 类型 | 来源 |
|------|------|------|
| `HeightRT` | Texture2D | User.HeightRT（R16f 深度） |
| `HeightCaptureZ` | float | User.HeightCaptureZ |
| `FieldOrigin` | float3 | User.FieldOrigin |
| `FieldExtent` | float3 | User.FieldExtent |
| `GroundOffset` | float | User.GroundOffset |
| `GroundBlendHeight` | float | User.GroundBlendHeight（姿态对齐 + 旋转阻尼阈值） |

**HeightRT UV 轴向约定**

```
U = (Position.y - (FieldOrigin.y - FieldExtent.y)) / (FieldExtent.y * 2)
V = ((FieldOrigin.x + FieldExtent.x) - Position.x) / (FieldExtent.x * 2)
```
（与 SceneCaptureComponent2D 朝向一致：U 对应 WorldY，V 反向对应 WorldX）

**逻辑流程**

```
groundZ = HeightCaptureZ - HeightRT.Sample(uv).r + GroundOffset
HeightAbove = Position.z - groundZ

─── 速度向下权重（核心）───────────────────────────────────────────
downWeight = smoothstep(0, FallBlendSpeed=30cm/s, -InVelocity.z)
  向上飞=0，水平≈0，正常下落≥1

─── 姿态对齐权重 t ─────────────────────────────────────────────
distWeight = 1 - smoothstep(0, GroundBlendHeight, HeightAbove)
t = distWeight × downWeight
  （起飞时 downWeight=0 → t=0 → 完全不介入）

─── 地形法线（仅 t>0.01 才采样）───────────────────────────────
  有限差分（eps=2/256），计算 tangentX/tangentY → cross → terrainNormal
  双面判断：选与当前朝上轴更近的一侧
  swing 四元数：curUp → targetUp，保留 Yaw

─── 旋转阻尼 ───────────────────────────────────────────────────
spinDistDamp = 1 - smoothstep(0, GroundBlendHeight, HeightAbove)
spinDamp = 1 - spinDistDamp × downWeight
  （向上飞：spinDamp=1，自由翻飞；向下+近地：spinDamp→0，停转）

─── 输出 ────────────────────────────────────────────────────────
OutPosition    = (x, y, max(z, groundZ))         地面 clamp
OutOrientation = nlerp(InOrientation, TargetQuat, t)
OutVelocity    = InVelocity（速度由速度场模块管理）
OutRotVelocity = RotationalVelocity × spinDamp

─── 落地完全静止 ────────────────────────────────────────────────
if (Position.z <= groundZ && InVelocity.z < 0):
    OutVelocity = 0, OutRotVelocity = 0, OutOrientation = TargetQuat
```

**可调阈值**

| 参数 | 来源 | 含义 |
|------|------|------|
| `FallBlendSpeed` | 硬编码 30 cm/s | 向下速度超过此值才完全启用贴地逻辑 |
| `GroundBlendHeight` | User.GroundBlendHeight | 距地多高开始姿态对齐 + 旋转阻尼（默认 18 cm） |

---

## 六、关键设计决策

### 6.1 速度场编码：RG8 + ZeroVelocity=(0.5, 0.5)

- 无速度时清除颜色为 `(0.5, 0.5, 0, 1)`
- 解码：`normVel = (rg - 0.5) * 2`，范围 [-1, 1]
- 编解码基准 `WindMaxSpeed` 必须全局一致，因此放在 `ULeafFieldSettings`

### 6.2 高度图：R16f，只拍一次

- 假设地形静态；`bHeightCaptured` 标记防止重复 SceneCapture
- 若需运行时改地形，将 `bHeightCaptured = false` 重置即可触发重拍
- 精度：R16f ≈ 65536 不同高度值，1cm 分辨率下覆盖 655m 高差

### 6.3 Field 休眠：ActiveFields 为空时零 Tick 开销

```cpp
if (ActiveFields.Num() == 0) return;   // Subsystem::Tick 提前退出
```

没有任何 Field 处于 Active 状态时，速度场管线完全跳过。

### 6.4 姿态对齐：仅速度向下时介入

`downWeight` 确保叶片被风吹起时完全自由翻转，只有真正在下落阶段才开始对齐地形法线，避免起飞时姿态被强制拉回地面产生视觉僵硬感。

### 6.5 Niagara 编译延迟处理

首次使用 Niagara System 时可能还在异步编译（shader），`ActivateField` 检测到 `HasOutstandingCompilationRequests()` 时先注册 Subsystem，等 `OnSystemCompiled` 回调后再真正 `Activate(true)`，确保 Spawn Burst 读到正确参数。

---

## 七、扩展指南

### 添加新 Field 参数

1. 在 `ALeafInteractionField.h` 添加 `UPROPERTY`
2. 在 `LeafInteractionField.cpp` 的 `LeafFieldNiagara` 命名空间添加常量名
3. 在 `PushStaticParams` 中调用 `SetVariableFloat/Vec3/...`（若参数每帧变化则改在 `PushDynamicParams`）
4. 在 Niagara System 中添加对应 `User.XXX` 参数并在 HLSL 中使用

### 支持多 Source 叠加速度

当前 `SplatPass` 取最大速度，升级路径：Ping-Pong 双 RT 多次 DrawMaterial 叠加（已在代码注释中标注 `// 升级路径`）。

### 运行时动态叶片密度

`LeafCount` 是 Spawn Burst 参数，运行时修改需要重新 `Activate(true)` 触发重新 Spawn。

---

## 八、依赖关系

```
LeafField 模块
├── Engine
├── CoreUObject
├── Niagara          （UNiagaraComponent, UNiagaraFunctionLibrary）
├── RenderCore       （UTextureRenderTarget2D）
└── DeveloperSettings（UDeveloperSettings 基类）

内容资产（Content/LeafField/）
├── RT_VelocityField  速度场 RG8 渲染目标
├── M_FluidSplat      Splat 材质（参数：SplatCenterUV / SplatRadiusUV / SplatVelocity）
└── N_LeafField       Niagara System（含上述两个 HLSL Update 模块）
```
