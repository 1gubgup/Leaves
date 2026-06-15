# LeafField 插件架构文档

> 版本：1.3 | 引擎：Unreal Engine 5 | 模块类型：Runtime
>
> 本文档面向开发者，记录所有模块的设计意图、数据流、参数含义和扩展方法。

---

## 一、插件概述

LeafField 是一个可拖入关卡的地面落叶交互插件，核心交互循环如下：

```
角色移动 → 速度写入全局 VelocityRT → Niagara 粒子采样 RT 获得风速 → 叶子被扇起
叶子下落 → 采样 HeightRT 对齐地形法线 → 贴地静止
```

整个系统由四个 C++ 类协作完成，详见第三章。

---

## 二、目录结构

```
Plugins/LeafField/
├── LeafField.uplugin
├── Source/LeafField/
│   ├── LeafField.Build.cs
│   ├── Public/
│   │   ├── LeafFieldSettings.h              全局项目设置（UDeveloperSettings）
│   │   ├── LeafFieldSubsystem.h             世界子系统（注册中心 + 速度场管线）
│   │   ├── LeafInteractionField.h           Field Actor（一块可铺叶的区域）
│   │   └── LeafInteractionSourceComponent.h 扰动源组件（挂在角色上）
│   └── Private/
│       ├── LeafFieldModule.cpp
│       ├── LeafFieldSettings.cpp
│       ├── LeafFieldSubsystem.cpp
│       ├── LeafInteractionField.cpp
│       └── LeafInteractionSourceComponent.cpp
├── README.md                                美术使用手册
└── ARCHITECTURE.md                          本文档
```

---

## 三、核心模块详解

### 3.1 ULeafFieldSettings — 全局项目设置

**编辑器路径**：项目设置 → 插件 → Leaf Field  
**存储位置**：`Config/DefaultGame.ini`（`config = Game, defaultconfig`）  
**读取时机**：Subsystem 的 `Initialize()` 里读取一次并缓存，运行中不会再读。改动后须重启 PIE。

这里**只放必须全局一致**的参数——即编码端（C++ Splat）和解码端（Niagara HLSL）都要用同一个值的参数，或者所有 Field 共享同一张 RT 资源的参数。

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `HeightRTSize` | int32 | 256 px | 高度图 RT 分辨率。每个 Field 各自创建一张，越大贴地法线越精细，内存占用线性增加。典型值：128（低精度）/ 256（默认）/ 512（高精度大地形）。改后重启 PIE。 |
| `VelocityFieldWidth` | float | 1000 cm | 速度场正方形边长，以本地玩家 Pawn 为中心。超出此范围的粒子不受风场影响。典型值：1000~2000 cm。 |
| `VelocityFieldRTSize` | int32 | 256 px | 速度场 RT 分辨率。越高速度贴图越细腻，但 GPU 带宽开销增大。典型值：128（省带宽）/ 256（默认）。改后重启 PIE。 |
| `WindMaxSpeed` | float | 1000 cm/s | RG8 速度编码基准。Splat 端写入 `NormVel = rawVel / WindMaxSpeed`，Niagara 解码时乘回来。**编解码必须用同一个值，轻易不要修改。** |

---

### 3.2 ULeafFieldSubsystem — 世界子系统

继承自 `UTickableWorldSubsystem`，是插件的**数据流枢纽**，不做渲染、不持有叶子状态，只负责：

1. **注册中心**：维护两张弱引用列表
   - `Sources`：`ULeafInteractionSourceComponent` 列表（角色扰动源）
   - `ActiveFields`：当前处于 Active 状态的 Field 列表

2. **速度场管线**（每帧，仅 `ActiveFields.Num() > 0` 时执行，否则提前退出节省 Tick 开销）：
   ```
   Step 1: ClearRT(ZeroVelocityColor = (0.5, 0.5, 0, 1))  清空上帧数据
   Step 2: SplatPass()   从 Sources 中取最大速度，DrawMaterial 写入 VelocityRT
   Step 3: 遍历 ActiveFields → PushDynamicParams(VelocityFieldCenter)
   ```

3. **VelocityFieldCenter 跟随**：每帧取本地 `PlayerController → Pawn` 的 XY 位置，写入 `VelocityFieldCenter`，使速度场始终以玩家为中心。无合法 Pawn 时保持上帧位置不变。

#### SplatPass 详解

- **最大速度策略**：遍历所有有效 Source，比较 `CachedVelocityXY` 的模长，只取最大的那个做一次 DrawMaterial Splat。逻辑简单，单 Source 场景下行为完全符合预期。
- **编码方式**：`NormVel = Velocity.XY / WindMaxSpeed`，以 `(0.5, 0.5)` 为零速中心写入 RG8。
- **Splat 材质**：`/LeafField/LeafField/M_FluidSplat`，参数：`SplatCenterUV`、`SplatRadiusUV`、`SplatVelocity`（已编码的 RG 值）。路径硬编码，不要移动或重命名。

#### 关键接口

```cpp
// 注册 / 注销（由 SourceComponent 的 BeginPlay/EndPlay 自动调用）
void RegisterSource(ULeafInteractionSourceComponent*)
void UnregisterSource(ULeafInteractionSourceComponent*)

// 激活 / 休眠通知（由 ALeafInteractionField 的 ActivateField/DeactivateField 调用）
void NotifyFieldActivated(ALeafInteractionField*)
void NotifyFieldDeactivated(ALeafInteractionField*)

// Field 在 PushStaticParams 里查询全局参数
UTextureRenderTarget2D* GetVelocityRT()      // 全局速度场 RT
const FVector& GetVelocityFieldCenter()       // 当前帧场中心（每帧变化）
float GetVelocityFieldWidth()                 // 从 Settings 缓存
float GetWindMaxSpeed()                       // 从 Settings 缓存
```

#### Tick 条件

```cpp
virtual bool IsTickable() const override { return true; }
virtual bool IsTickableInEditor() const override { return false; }   // 编辑器不 Tick
virtual bool IsTickableWhenPaused() const override { return false; } // 暂停时不 Tick
```

---

### 3.3 ALeafInteractionField — Field Actor

代表"一块铺有落叶的区域"，可拖入关卡，每个 Field 独立控制自己的叶子和风参数。

#### 组件树

```
SceneRoot (USceneComponent)
├── FieldBox         (UBoxComponent)              编辑器黄框，叶子铺设范围可视化，无碰撞
├── NiagaraComponent (UNiagaraComponent)          叶子粒子系统，bAutoActivate=false
└── HeightCapture    (USceneCaptureComponent2D)   朝下正交相机，拍地面深度
```

#### 状态机

```
             ActivateField()
  Dormant ──────────────────▶ Active
    ▲                           │
    └───────────────────────────┘
          DeactivateField()
```

- **Dormant**：NiagaraComponent 未激活，Subsystem 不推参数给此 Field
- **Active**：Subsystem 每帧调用 `PushDynamicParams`；静态参数在进入 Active 时推送一次

两个接口均为**幂等**：重复调用无副作用。

`bAutoActivateOnBeginPlay = true`（默认）时 `BeginPlay` 自动调 `ActivateField()`，无需外部蓝图。

#### Niagara 编译延迟处理

首次加载 NiagaraSystem 时可能仍在异步编译 shader。此时若直接 `Activate(true)`，Spawn Burst 会在参数就绪前触发，叶子无法正确初始化。

处理流程：
```
ActivateField()
  └── HasOutstandingCompilationRequests() == true?
        ├── Yes → 先设 State=Active、注册 Subsystem，绑 OnSystemCompiled 回调，等待
        └── No  → PushStaticParams() → PushDynamicParams() → NiagaraComponent->Activate(true)

OnNiagaraCompiled() 回调
  └── PushStaticParams() → PushDynamicParams() → NiagaraComponent->Activate(true)
```

#### 高度图捕获

- **格式**：`RTF_R16f`，精度约 1cm/步，65536 阶，覆盖 655m 高差
- **拍摄时机**：`ActivateField()` 首次调用时（`bHeightCaptured == false`），之后复用，不再重拍
- **隐藏 Pawn**：拍摄前自动将场景内所有 Pawn 加入 `HiddenActors`，避免角色深度烤入高度图
- **强制重拍**：运行时改地形后，将 `bHeightCaptured = false` 重置，下次 `ActivateField()` 时自动重拍

#### 参数推送分类

| 函数 | 调用时机 | 推送内容 |
|------|----------|----------|
| `PushStaticParams()` | 激活时 / `RefreshParams()` 时 / 编辑器改属性时（`PostEditChangeProperty`） | 除 VelocityFieldCenter 外的全部参数 |
| `PushDynamicParams(Center)` | Subsystem 每帧调用 | 仅 `User.VelocityFieldCenter`（每帧跟随 Pawn 变化） |

编辑器热推：`PostEditChangeProperty` 触发后，若当前 State 为 Active，自动调 `PushStaticParams()`，Details 面板改值时无需停止 PIE 即可实时预览。

#### 完整参数表

**Asset 类**

| 参数 | 类型 | 默认值 | Niagara 变量 | 说明 |
|------|------|--------|-------------|------|
| `LeafSystem` | UNiagaraSystem* | nullptr | — | 必须指向 N_LeafField |
| `bAutoActivateOnBeginPlay` | bool | true | — | BeginPlay 自动激活 |
| `LeafMeshes[4]` | FLeafMeshEntry[] | — | `User.LeafMesh0~3` + `User.MeshThresholds` | 固定 4 个槽位，Mesh 空则权重锁 0 |

**Layout 类**

| 参数 | 类型 | 默认值 | Niagara 变量 | 说明 |
|------|------|--------|-------------|------|
| `FieldExtent` | FVector | (500, 500, 10) cm | `User.FieldExtent` | 叶子铺设半尺寸，XY=撒布半径，Z=可视化框高 |

**Appearance 类**

| 参数 | 类型 | 默认值 | Niagara 变量 | 说明 |
|------|------|--------|-------------|------|
| `LeafCount` | int32 | 1024 | `User.LeafCount` | Spawn Burst 数量，运行时改需重新 Activate |
| `GroundOffset` | float | 5 cm | `User.GroundOffset` | 叶片贴地安全距离，防止叶子陷入地面 |

**Wind 类（每个 Field 独立）**

| 参数 | 类型 | 默认值 | 范围 | Niagara 变量 | 说明 |
|------|------|--------|------|-------------|------|
| `WindStrength` | float | 1.0 | 0~3 | `User.WindStrength` | 本 Field 风强度倍率。速度场解码后乘此值，0=无风，2=双倍 |
| `WindLift` | float | 0.05 | 0~1 | `User.WindLift` | 水平风速转垂直上抬力的比例。0=只水平飘，1=上抬力=水平风强 |
| `WindResponseSpeed` | float | 10.0 (1/s) | 0.5~50 | `User.WindResponseSpeed` | 帧率无关响应速度。`Alpha = saturate(DeltaTime × Speed)`，10≈0.1s 完全跟上 |
| `WindSpinImpulse` | float | 1.0 | 0~5 | `User.WindSpinImpulse` | 被风踢起时的旋转冲量强度，由速度增量驱动，0=不翻滚 |

**Advanced 类**

| 参数 | 类型 | 默认值 | 范围 | Niagara 变量 | 说明 |
|------|------|--------|------|-------------|------|
| `HeightCaptureZOffset` | float | 2000 cm | 100~10000 | `User.HeightCaptureZ`（世界 Z） | 高度相机在 Actor 原点上方的拍摄高度。需高于场内最高地形，否则相机嵌入地形导致深度图全黑 |
| `GroundBlendHeight` | float | 10 cm | 1~100 | `User.GroundBlendHeight` | 贴地过渡区高度，叶子距地面低于此值时开始对齐法线并阻尼旋转 |

---

### 3.4 ULeafInteractionSourceComponent — 扰动源组件

挂在角色（或任何会扰叶的 Actor）身上，不直接读写 RT，不依赖 MovementComponent。

#### 速度计算逻辑

```
TickComponent:
  rawVel = (CurrentLocation - PrevLocation) / DeltaTime   // 位置差分，cm/s
  
  if bUsePeakHold:
    // 峰值保持：新速度更快时直接采用（起步零延迟）；慢时低通衰减（停步柔和）
    if |rawVel| > |CachedVelocityXY|:
        CachedVelocityXY = rawVel.XY * VelocityStrength
    else:
        CachedVelocityXY = lerp(CachedVelocityXY, rawVel.XY * VelocityStrength,
                                DeltaTime / VelocityDecayTime)
  else:
    // 纯低通：起步和停步均有延迟
    CachedVelocityXY = lerp(CachedVelocityXY, rawVel.XY * VelocityStrength,
                            DeltaTime / VelocityDecayTime)
```

#### 参数表

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `BrushRadiusWorld` | float | 175 cm | Splat 笔刷半径（**世界单位，cm**）。Subsystem 内自动换算为 UV：`RadiusUV = BrushRadiusWorld / VelocityFieldWidth`，不受 VelocityFieldWidth 变化影响，典型值 100~350 cm |
| `VelocityStrength` | float | 1.0 | 速度倍率，BOSS 等大型单位可调至 2~3 |
| `VelocityDecayTime` | float | 0.25 s | 停步后速度衰减时间常数，越小停步越干脆 |
| `bUsePeakHold` | bool | true | 峰值保持模式，推荐保持 true |

---

## 四、整体数据流

```
[角色 Pawn]
    └── ULeafInteractionSourceComponent
          │  TickComponent: 位置差分 → CachedVelocityXY（含峰值保持 + 低通）
          │  BeginPlay:  RegisterSource(Subsystem)
          │  EndPlay:    UnregisterSource(Subsystem)
          ▼
[ULeafFieldSubsystem]  ← UTickableWorldSubsystem，每帧 Tick
    │
    │  1. VelocityFieldCenter = LocalPlayerPawn.Location.XY
    │
    │  2. 若 ActiveFields.Num() == 0 → 直接 return（零开销）
    │
    │  3. ClearRT(VelocityRT, ClearColor=(0.5,0.5,0,1))
    │
	│  4. SplatPass:
	│       取 Sources 中 |CachedVelocityXY| 最大者（BestSrc）
	│       SplatCenterUV = WorldToVelocityUV(BestSrc.Location)
	│       SplatRadiusUV = BestSrc.BrushRadiusWorld / VelocityFieldWidth
	│       SplatVelocity = BestSrc.CachedVelocityXY / WindMaxSpeed
	│       DrawMaterialToRenderTarget(VelocityRT, SplatMID)
    │
    │  5. for each ActiveField:
    │         Field.PushDynamicParams(VelocityFieldCenter)
    │           └── NiagaraComponent.SetVariableVec3("User.VelocityFieldCenter", Center)
    │
    ▼
[ALeafInteractionField]
    │
    │  PushStaticParams（激活时 / RefreshParams / 编辑器改属性）:
    │    ├── 全局（来自 Subsystem）: VelocityRT / VelocityFieldWidth / WindMaxSpeed
    │    ├── Wind（per-Field）: WindStrength / WindLift / WindResponseSpeed / WindSpinImpulse
    │    ├── Field 自身: HeightRT / HeightCaptureZ / FieldOrigin / FieldExtent
    │    │               GroundOffset / GroundBlendHeight
    │    └── 美术: LeafCount / LeafMesh0~3 / MeshThresholds
    │
    │  PushDynamicParams（每帧，来自 Subsystem）:
    │    └── VelocityFieldCenter
    │
    ▼
[Niagara System: N_LeafField]
    ├── Init Module（Spawn Burst，每次 Activate 触发一次）
    │       随机 XY → 世界坐标
    │       采样 HeightRT → 贴地 Z
    │       随机 Yaw → 平躺姿态四元数
    │       RandMesh + MeshThresholds → MeshIndex
    │
    └── Update Module（每帧，每个粒子）
            ├── 速度场模块: VelocityRT → 解码风速 → 一阶低通 → OutVelocity
            │               速度增量 → smoothstep → 旋转冲量
            └── 贴地模块:   HeightRT → groundZ → 姿态对齐 + 位置 clamp + 旋转阻尼
```

---

## 五、Niagara HLSL 模块详解

### 5.1 Init 模块 — 叶片初始化（Spawn Burst）

每次 `NiagaraComponent->Activate(true)` 后触发一次，决定每片叶子的初始位置、朝向和 Mesh。

**输入 Binding**

| 参数名 | 类型 | 来源 | 说明 |
|--------|------|------|------|
| `RandX`, `RandY`, `RandYaw` | float [0,1] | Niagara Random Float | 初始化随机数 |
| `RandMesh` | float [0,1] | Niagara Random Float | Mesh 选择用随机数 |
| `HeightRT` | Texture2D | `User.HeightRT` | R16f 深度图，SCS_SceneDepth |
| `HeightCaptureZ` | float | `User.HeightCaptureZ` | 相机世界 Z（= Actor.Z + HeightCaptureZOffset） |
| `FieldOrigin` | float3 | `User.FieldOrigin` | Actor 世界位置 |
| `FieldExtent` | float3 | `User.FieldExtent` | 半尺寸（cm） |
| `GroundOffset` | float | `User.GroundOffset` | 贴地安全距离（cm） |
| `MeshThresholds` | float3 | `User.MeshThresholds` | 槽 0/1/2 的归一化累积阈值（xyz） |

**逻辑**

```hlsl
// 1. 随机世界坐标（在 FieldExtent 范围内均匀分布）
float worldX = (FieldOrigin.x - FieldExtent.x) + RandX * (FieldExtent.x * 2.0);
float worldY = (FieldOrigin.y - FieldExtent.y) + RandY * (FieldExtent.y * 2.0);

// 2. 采样高度图得到贴地 Z
//    UV 轴向：U=worldY 方向，V=反向 worldX 方向（与 SceneCapture 相机朝向一致）
float2 uv    = float2(RandY, 1.0 - RandX);
float worldZ = HeightCaptureZ - HeightRT.SampleTexture2D(uv).r + GroundOffset;

// 3. 随机 Yaw，平躺姿态（绕 Z 轴旋转的单位四元数）
float yaw = RandYaw * 6.28318;
OutPosition    = float3(worldX, worldY, worldZ);
OutOrientation = float4(0, 0, sin(yaw * 0.5), cos(yaw * 0.5));

// 4. 多 Mesh 选择（累积阈值区间判断）
//    MeshThresholds.xyz = 槽 0/1/2 的累积上限，槽 3 为隐式兜底
int meshIdx = 3;
if      (RandMesh < MeshThresholds.x) meshIdx = 0;
else if (RandMesh < MeshThresholds.y) meshIdx = 1;
else if (RandMesh < MeshThresholds.z) meshIdx = 2;
OutMeshIndex = meshIdx;   // 绑定到 Mesh Renderer 的 Mesh Index Binding
```

**MeshThresholds 计算（C++ 侧，PushStaticParams 中）**

```cpp
// 收集有效权重（Mesh 为空的槽位强制 0）
float SlotWeights[4] = { 0 };
float TotalWeight = 0;
for (int i = 0; i < 4; ++i) {
    if (LeafMeshes[i].Mesh == nullptr) continue;
    SlotWeights[i] = (LeafMeshes[i].Weight > 0) ? LeafMeshes[i].Weight : 1.f;
    TotalWeight += SlotWeights[i];
}
// 累积归一化，打包进 FVector（xyz = 槽 0/1/2 上限，槽 3 隐式兜底）
FVector Thresholds = FVector::ZeroVector;
float Cumulative = 0;
for (int i = 0; i < 3; ++i) {
    Cumulative += (TotalWeight > 0) ? SlotWeights[i] / TotalWeight : 0;
    Thresholds[i] = FMath::Min(Cumulative, 1.f);
}
NiagaraComponent->SetVariableVec3(N_MeshThresholds, Thresholds);
// 空槽用上一个有效 Mesh 填占位（防 Renderer null），但其权重为 0 永不会被选中
```

---

### 5.2 Update 模块 — 速度场驱动（每帧）

**输入 Binding**

| 参数名 | 类型 | 来源 | 说明 |
|--------|------|------|------|
| `VelocityRT` | Texture2D | `User.VelocityRT` | RG8 速度场 |
| `VelocityFieldCenter` | float3 | `User.VelocityFieldCenter` | 场中心（每帧更新） |
| `VelocityFieldWidth` | float | `User.VelocityFieldWidth` | 场覆盖边长 |
| `WindMaxSpeed` | float | `User.WindMaxSpeed` | 解码基准 |
| `WindStrength` | float | `User.WindStrength` | 本 Field 倍率 |
| `WindLift` | float | `User.WindLift` | 水平转上抬比例 |
| `WindResponseSpeed` | float | `User.WindResponseSpeed` | 响应速度（1/s） |
| `WindSpinImpulse` | float | `User.WindSpinImpulse` | 旋转冲量强度 |
| `UniqueID` | uint | 粒子内置 | per-particle 唯一 ID，用于 hash 随机旋转轴 |
| `DeltaTime` | float | Niagara 内置 | 帧时间 |

**完整逻辑**

```hlsl
// ── 1. 世界坐标 → 速度场 UV ────────────────────────────────────────
float HalfW = VelocityFieldWidth * 0.5;
float2 velUV = float2(
    (Position.x - (VelocityFieldCenter.x - HalfW)) / VelocityFieldWidth,
    (Position.y - (VelocityFieldCenter.y - HalfW)) / VelocityFieldWidth);

// 默认透传（场外叶子不受影响）
OutVelocity           = InVelocity;
OutRotationalVelocity = RotationalVelocity;
if (!all(velUV >= 0.0 && velUV <= 1.0)) return;

// ── 2. 采样并解码速度场 ──────────────────────────────────────────────
float4 velSample;
VelocityRT.SampleTexture2D(velUV, 0.0, velSample);
float2 normVel = (velSample.rg - 0.5) * 2.0;   // [-1, 1]
float  windMag = length(normVel) * WindMaxSpeed * WindStrength;

// ── 3. 方向钳位到单位圆（防止 RG 对角超界时方向错误）─────────────────
float normLenSq = dot(normVel, normVel);
if (normLenSq > 1.0) normVel *= rsqrt(normLenSq);

// ── 4. 目标风速（XY 水平 + Z 上抬）────────────────────────────────────
float3 windVel = float3(normVel * WindMaxSpeed * WindStrength,
                        windMag * WindLift);

// ── 5. 帧率无关一阶低通响应 ─────────────────────────────────────────────
// Alpha = saturate(DeltaTime * WindResponseSpeed)
// 60fps+DT=0.016, Speed=10 → Alpha≈0.16，约 6 帧（0.1s）完全跟上
float Alpha    = saturate(DeltaTime * WindResponseSpeed);
OutVelocity.xy = lerp(InVelocity.xy, windVel.xy, Alpha);
OutVelocity.z  = InVelocity.z + windVel.z;   // Z 分量直接叠加（保留重力）

// ── 6. per-particle 整数 hash → 随机旋转轴 ──────────────────────────
uint h = uint(UniqueID);
h ^= h >> 16; h *= 0x45d9f3bu;
h ^= h >> 16; h *= 0x45d9f3bu;
h ^= h >> 16;
float R1 = float(h & 0xFFFFu) / 65535.0 * 2.0 - 1.0;
h ^= h >> 13; h *= 0xb5297a4du; h ^= h >> 16;
float R2 = float(h & 0xFFFFu) / 65535.0 * 2.0 - 1.0;

// ── 7. 速度增量驱动旋转冲量 ─────────────────────────────────────────────
// speedDelta > 0 才有冲量，smoothstep 让小增量不触发翻滚
float speedDelta = max(0.0, length(OutVelocity) - length(InVelocity));
OutRotationalVelocity = RotationalVelocity
    + float3(R1, R2, 0) * WindSpinImpulse * smoothstep(0.0, 100.0, speedDelta);
```

**设计说明**

- **帧率无关**：`Alpha = saturate(DeltaTime × Speed)` 是标准离散化一阶低通，在任何帧率下响应时间一致。
- **Z 分量直接叠加**：不 lerp，因为 Z 已有重力下拉，直接叠加上抬力，让叶子被吹起时有爆发感。
- **旋转轴随机化**：用整数 hash（Murmur3 变体）替代 sin 随机，GPU 分支更少、分布更均匀，且每个粒子结果固定（基于 UniqueID）。
- **无方向噪声**：旧版有 per-particle 方向噪声扰动（`windVel.xy += R1*R2*windMag*NoiseScale`），已移除，效果更干净可控。

---

### 5.3 Update 模块 — 贴地/碰撞处理（每帧）

**输入 Binding**

| 参数名 | 类型 | 来源 | 说明 |
|--------|------|------|------|
| `HeightRT` | Texture2D | `User.HeightRT` | R16f 深度图 |
| `HeightCaptureZ` | float | `User.HeightCaptureZ` | 相机世界 Z |
| `FieldOrigin` | float3 | `User.FieldOrigin` | Actor 世界位置 |
| `FieldExtent` | float3 | `User.FieldExtent` | 半尺寸 |
| `GroundOffset` | float | `User.GroundOffset` | 贴地安全距离 |
| `GroundBlendHeight` | float | `User.GroundBlendHeight` | 过渡区高度 |

**HeightRT UV 轴向约定**

```
U = (Position.y - (FieldOrigin.y - FieldExtent.y)) / (FieldExtent.y * 2)
V = ((FieldOrigin.x + FieldExtent.x) - Position.x) / (FieldExtent.x * 2)
```
（SceneCaptureComponent2D 朝下，U 对应 WorldY，V 反向对应 WorldX）

**逻辑流程**

```hlsl
// ── 1. 获取地面 Z ────────────────────────────────────────────────────
float2 hUV   = /* 上方 UV 公式 */;
float groundZ = HeightCaptureZ - HeightRT.SampleTexture2D(hUV).r + GroundOffset;
float heightAbove = Position.z - groundZ;

// ── 2. 速度向下权重（核心：起飞时完全不介入）───────────────────────────
// FallBlendSpeed 硬编码 30cm/s；向上飞时 -InVelocity.z < 0 → downWeight=0
float downWeight = smoothstep(0.0, 30.0, -InVelocity.z);

// ── 3. 距地权重（近地才介入）──────────────────────────────────────────
float distWeight = 1.0 - smoothstep(0.0, GroundBlendHeight, heightAbove);

// ── 4. 综合权重（两者同时满足才真正对齐）──────────────────────────────
float t = distWeight * downWeight;

// ── 5. 地形法线（仅 t > 0.01 才采样，节省带宽）───────────────────────
float3 targetNormal = float3(0, 0, 1);   // 默认世界朝上
if (t > 0.01) {
    float eps = 2.0 / 256.0;
    // 有限差分：采样相邻四点，叉乘得法线
    float hL = HeightCaptureZ - HeightRT.SampleTexture2D(hUV - float2(eps, 0)).r;
    float hR = HeightCaptureZ - HeightRT.SampleTexture2D(hUV + float2(eps, 0)).r;
    float hD = HeightCaptureZ - HeightRT.SampleTexture2D(hUV - float2(0, eps)).r;
    float hU = HeightCaptureZ - HeightRT.SampleTexture2D(hUV + float2(0, eps)).r;
    float3 tangX = normalize(float3(2.0 * eps * FieldExtent.x * 2, 0, hR - hL));
    float3 tangY = normalize(float3(0, 2.0 * eps * FieldExtent.y * 2, hU - hD));
    targetNormal = normalize(cross(tangX, tangY));
    // 双面判断：选与叶子当前朝上轴更近的一侧
    float3 curUp = QuatRotate(InOrientation, float3(0, 0, 1));
    if (dot(targetNormal, curUp) < 0) targetNormal = -targetNormal;
}

// ── 6. 姿态 slerp（保留 Yaw，swing 对齐法线）─────────────────────────
float4 targetQuat = SwingToward(InOrientation, targetNormal);
OutOrientation    = nlerp(InOrientation, targetQuat, t);

// ── 7. 旋转阻尼（近地+向下时阻止翻滚）──────────────────────────────────
float spinDamp     = 1.0 - distWeight * downWeight;
OutRotVelocity     = RotationalVelocity * spinDamp;

// ── 8. 位置 clamp（不穿地）──────────────────────────────────────────
OutPosition = float3(Position.xy, max(Position.z, groundZ));

// ── 9. 完全落地 → 静止 ───────────────────────────────────────────────
if (Position.z <= groundZ && InVelocity.z < 0.0) {
    OutVelocity    = float3(0, 0, 0);
    OutRotVelocity = float3(0, 0, 0);
    OutOrientation = targetQuat;
}
```

**硬编码阈值说明**

| 名称 | 值 | 含义 |
|------|----|------|
| `FallBlendSpeed` | 30 cm/s | 向下速度超过此值才完全启用贴地逻辑 |
| `NormalEps` | 2/256 | 有限差分步长（UV 空间），对应 HeightRTSize=256 时约 2px |

---

## 六、Niagara User 参数汇总

Niagara System 中需声明的全部 User 参数，与 C++ 端 `LeafFieldNiagara` 命名空间的常量一一对应：

| User 参数名 | 类型 | 推送方 | 来源 |
|------------|------|--------|------|
| `User.VelocityRT` | Texture Object | Subsystem（通过 Field） | 全局速度场 RT |
| `User.VelocityFieldCenter` | Vector3 | Subsystem（每帧） | Pawn XY 位置 |
| `User.VelocityFieldWidth` | float | PushStaticParams | Settings |
| `User.WindMaxSpeed` | float | PushStaticParams | Settings |
| `User.WindStrength` | float | PushStaticParams | Field 属性 |
| `User.WindLift` | float | PushStaticParams | Field 属性 |
| `User.WindResponseSpeed` | float | PushStaticParams | Field 属性 |
| `User.WindSpinImpulse` | float | PushStaticParams | Field 属性 |
| `User.HeightRT` | Texture Object | PushStaticParams | Field 运行时创建 |
| `User.HeightCaptureZ` | float | PushStaticParams | Actor.Z + Offset |
| `User.FieldOrigin` | Vector3 | PushStaticParams | Actor 世界位置 |
| `User.FieldExtent` | Vector3 | PushStaticParams | Field 属性 |
| `User.GroundOffset` | float | PushStaticParams | Field 属性 |
| `User.GroundBlendHeight` | float | PushStaticParams | Field 属性 |
| `User.LeafCount` | int | PushStaticParams | Field 属性 |
| `User.LeafMesh0~3` | Static Mesh | PushStaticParams | LeafMeshes[] |
| `User.MeshThresholds` | Vector3 | PushStaticParams | 权重归一化计算 |

---

## 七、关键设计决策

### 7.1 速度场编码：RG8 + (0.5,0.5) 为零速

- 无速度时清除颜色为 `(0.5, 0.5, 0, 1)`，解码为 `(0, 0)`，叶子不受力
- 解码：`normVel = (rg - 0.5) * 2`，范围 [-1, 1]
- `WindMaxSpeed` 必须全局一致，放在 Settings 而非 Field，防止多 Field 场景下编解码不匹配

### 7.2 高度图：R16f，只拍一次

- 假设地形静态，`bHeightCaptured` 标记防止重复 SceneCapture
- R16f 精度：1cm 步长，覆盖约 655m 高差
- 拍摄前隐藏所有 Pawn，避免角色深度干扰

### 7.3 Field 休眠：ActiveFields 为空时零 Tick 开销

```cpp
// ULeafFieldSubsystem::Tick
if (ActiveFields.Num() == 0) return;
```

没有 Field 处于 Active 时，Subsystem Tick 中没有任何 RT 操作。

### 7.4 姿态对齐：只在下落时介入

`downWeight = smoothstep(0, 30cm/s, -Vz)`，起飞时 `downWeight=0`，姿态完全自由，避免被风吹起时叶子被强制拉回平躺产生视觉僵硬感。

### 7.5 WindResponseSpeed：帧率无关的一阶低通

| WindResponseSpeed | 约完全响应时间 | 效果描述 |
|---|---|---|
| 1 | ~1 s | 极度拖拽，像水中飘动 |
| 5 | ~0.2 s | 轻微惯性 |
| 10 | ~0.1 s | 推荐默认，轻微拖尾 |
| 20 | ~0.05 s | 几乎硬跟随 |

### 7.6 MeshThresholds 打包为 Vector3

4 个 Mesh 槽位的累积阈值本需 3 个 float（槽 3 为隐式兜底），打包进单个 `FVector` 推送，减少 User 参数数量，避免参数名分散导致 Niagara 侧对不齐。

### 7.7 Niagara 编译延迟处理

首次使用 NiagaraSystem 时异步编译 shader，`ActivateField` 检测到 `HasOutstandingCompilationRequests()` 后先设好状态、绑 `OnSystemCompiled` 回调，编译完成再推参并 `Activate(true)`，确保 Spawn Burst 读到正确参数。

---

## 八、扩展指南

### 8.1 添加新的 Field 参数

1. `LeafInteractionField.h`：添加 `UPROPERTY`
2. `LeafInteractionField.cpp`，`LeafFieldNiagara` 命名空间：添加 `static const FName N_XXX(TEXT("User.XXX"))`
3. `PushStaticParams()`：调用 `NiagaraComponent->SetVariableFloat/Vec3/...`（每帧变化的参数改放 `PushDynamicParams`）
4. Niagara System：添加对应 `User.XXX` 参数，在 HLSL 中使用

### 8.2 多 Source 速度叠加（待实现）

当前 `SplatPass` 采用**最大速度单 Source** 策略，只有速度最快的那个 Source 生效。

如需支持多 Source 同时扰动，可选方案：

**方案 A：向量叠加（CPU 合并，单次 DrawMaterial）**
```
SumVelocity = Σ(Source_i.velocity)，限幅到 WindMaxSpeed
SplatUV = 速度幅度加权质心
RadiusUV = max(Source_i.BrushRadiusWorld / VelocityFieldWidth)
DrawMaterialToRenderTarget(VelocityRT, SplatMID)
```

**方案 B：独立 Splat（每 Source 各画一次，需改材质混合模式）**
```
// M_FluidSplat Blend Mode → Additive，RT 清色改为 (0,0,0,1)
// Niagara 解码改为：normVel = rg * 2.0
For each Source:
    SplatMID_i.SetParams(Source_i)
    DrawMaterialToRenderTarget(VelocityRT, SplatMID_i)
// 每个 Source 需独立的 UMaterialInstanceDynamic 实例（SplatMIDPool）
```

### 8.3 运行时动态叶片密度

`LeafCount` 是 Spawn Burst 参数，运行时修改后需重新 `Activate(true)` 才能重新 Spawn：
```cpp
Field->DeactivateField();
Field->LeafCount = NewCount;
Field->ActivateField();
```
或调用 `RefreshParams()` + 手动 `NiagaraComponent->Activate(true)`。

---

## 九、依赖关系

```
LeafField 模块
├── Engine
├── CoreUObject
├── Niagara          (UNiagaraComponent, UNiagaraFunctionLibrary)
├── RenderCore       (UTextureRenderTarget2D)
└── DeveloperSettings(UDeveloperSettings 基类)

内容资产（Content/LeafField/）—— 路径硬编码，不要移动或重命名
├── RT_VelocityField  速度场 RG8 渲染目标（ClearColor 必须为 (0.5,0.5,0,1)）
├── M_FluidSplat      Splat 材质（参数：SplatCenterUV / SplatRadiusUV / SplatVelocity）
└── N_LeafField       Niagara System（含上述两个 HLSL Update 模块 + Init 模块）
```
