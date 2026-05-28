# 落叶交互速度场

角色驱动的极简风场：用一张 RT 记录角色周围的瞬时速度，Niagara 粒子按自身世界位置采样并叠加到速度。**当前为无尾流极简管线**。

---

## 1. 管线

每帧在 `ULeafInteractionFieldSubsystem::Tick` 内：

```
CaptureCenter ← 本地 Pawn 位置
    │
    ▼
[Clear]  RT 清成 (0.5, 0.5, 0, 1) — 编码后的零速度
    │
    ▼
[Splat]  每个 Source 在 UV 位置画一个高斯笔刷 quad（AlphaComposite，写 RG）
    │
    ▼
[Push]   推 RT / CaptureCenter / CaptureWidth / WindStrength / VerticalLift / MaxWindSpeed
         到所有带 Tag=LeafField 的 NiagaraComponent（缓存，不每帧扫场景）
```

---

## 2. 编码方案（为什么 RT 基色是 (0.5,0.5,0,1)）

Canvas 写 RT 路径默认 LDR 钳制：**负值会被截到 0**。所以把 `[-1,1]` 偏移到 `[0,1]`：

| 阶段 | 公式 |
|---|---|
| C++ 归一化 | `NormVel = RawVel / VelocityScale ∈ [-1, 1]` |
| Splat 材质编码 | `输出 = NormVel*0.5 + 0.5`（笔刷中心） |
| RT 清零基准 | `(0.5, 0.5, 0, 1)` |
| Niagara 解码 | `NormVel = (Sample.rg - 0.5) * 2` |

**预乘透明度混合：** `Dst = Src.rgb + Dst*(1-Src.a)`
- 笔刷中心 `Src.a=1` → 直接覆盖为编码值 ✓
- 笔刷外 `Src.a=0` → 保留 0.5 基准 ✓

---

## 3. 代码文件（`Source/Leaves/LeafField/`）

| 文件 | 作用 |
|---|---|
| `LeafInteractionFieldSubsystem.h/.cpp` | World Subsystem，加载 RT/材质，每帧 Clear→Splat→Push；维护 NiagaraComponent 缓存（OnActorSpawned 自动入缓存）。 |
| `LeafInteractionSourceComponent.h/.cpp` | 扰动源组件，BeginPlay 登记到 Subsystem；位置差分算 XY 速度（cm/s）。 |

---

## 4. Content 资产（`Content/LeafField/`）

| 资产 | 必备配置 |
|---|---|
| `RT_VelocityField_A` | **RGBA16f**、256×256、Clamp、ClearColor=(0.5,0.5,0,1) |
| `M_FluidSplat` | **Domain=Surface**、**Blend Mode=AlphaComposite (Premultiplied Alpha)**、**Shading Model=Unlit** |

### M_FluidSplat 节点结构

```
TexCoord(0) ── Sub(SplatCenterUV) ── Length ── Div(SplatRadiusUV) ── Mul(-4) ── Exp ──┬─► Opacity
                                                                                       │
SplatVelocity ── Mul(0.5) ── Add(0.5) ───────────────────── Mul ─────► Emissive Color  │
                                                             ▲                         │
                                                             └─────────────────────────┘
```

**材质参数：**

| 参数 | 类型 | C++ 写入位置 |
|---|---|---|
| `SplatCenterUV` | Vector | `SplatPass`，每个 Source |
| `SplatRadiusUV` | Scalar | `SplatPass`，每个 Source |
| `SplatVelocity` | Vector | `SplatPass`，每个 Source（未编码 `[-1,1]`） |

> ⚠️ **Material Domain 必须 Surface**。设成 User Interface 会强制 LDR 钳制，所有 `<0.5` 的编码值（即负方向速度）会被截断为 0.5，表现为"只有 +X+Y 能吹叶子"。

---

## 5. Niagara 端接入

落叶 Niagara（如 `N_朝向设置`）需要：

1. **Emitter** 切 GPUCompute Sim，设 Fixed Bounds（覆盖落叶区域，例如 ±1000）
2. **User Parameters**（名字与 cpp `LeafFieldParam::N_*` 完全一致）：

| 名称 | 类型 |
|---|---|
| `User.VelocityRT` | Texture |
| `User.CaptureCenter` | Vector |
| `User.CaptureWidth` | Float |
| `User.WindStrength` | Float |
| `User.VerticalLift` | Float |
| `User.MaxWindSpeed` | Float |

3. **Particle Update** 加 Scratch Pad（Custom HLSL）：

```hlsl
// 输入：VelocityRT(Tex), CaptureCenter, CaptureWidth, WindStrength, VerticalLift, MaxWindSpeed
//      InPos, InVel, DeltaTime, UniqueID
// 输出：OutVel

float HalfW   = CaptureWidth * 0.5;
float2 UV     = (InPos.xy - (CaptureCenter.xy - HalfW)) / CaptureWidth;

float3 WindVel = float3(0, 0, 0);
if (UV.x >= 0.0 && UV.x <= 1.0 && UV.y >= 0.0 && UV.y <= 1.0)
{
    float4 Sample = float4(0,0,0,0);
    VelocityRT.SampleTexture2D(UV, Sample);

    float2 NormVel = (Sample.rg - 0.5) * 2.0;        // 解码
    float  NLen    = length(NormVel);
    if (NLen > 1.0) NormVel /= NLen;                  // 编码越界保护

    float2 WindXY  = NormVel * MaxWindSpeed;
    float  WindLen = length(WindXY);
    WindVel = float3(WindXY, WindLen * VerticalLift);

    // 粒子个体扰动（打破整齐感）：方向 + 响应速度
    const float NoiseScale = 0.25;
    float2 RandDir;
    RandDir.x = frac(sin(float(UniqueID) * 127.1) * 43758.5) * 2.0 - 1.0;
    RandDir.y = frac(sin(float(UniqueID) * 311.7) * 53421.3) * 2.0 - 1.0;
    WindVel.xy += RandDir * WindLen * NoiseScale;
}

// 个体响应时间（[0.05, 0.35] s 随机）
const float RespMin = 0.05, RespRange = 0.30;
float RandResp = frac(sin(float(UniqueID) * 73.1) * 13758.5);
float Alpha = saturate(DeltaTime / (RespMin + RandResp * RespRange)) * WindStrength;

OutVel = lerp(InVel, WindVel, Alpha);
```

4. NiagaraActor 加 Tag = `LeafField`，Subsystem 才会推参数

---

## 6. 调感觉的 5 个旋钮 ★

其他参数固定，调感觉就这 5 个：

### Subsystem（World Settings 或 Subsystem Detail）

| 参数 | 默认 | 想要的效果 → 调到 |
|---|---|---|
| `WindStrength` | 1.0 | 风更猛 → 1.5~2.5；风更弱 → 0.5 |
| `MaxWindSpeed` | 800 cm/s | 飞更快 → 1200~1600 |
| `VerticalLift` | 0.3 | 飘更高 → 0.5~0.8 |

### Source（角色 BP 组件）

| 参数 | 默认 | 想要的效果 → 调到 |
|---|---|---|
| `BrushRadiusUV` | 0.35 | 影响圈更大 → 0.45~0.6 |
| `VelocityStrength` | 1.0 | 角色轻动也能吹 → 1.5~2.5 |

### HLSL 常量（一般不动）

| 常量 | 默认 | 含义 |
|---|---|---|
| `NoiseScale` | 0.25 | 方向扰动幅度 |
| `RespMin` / `RespRange` | 0.05 / 0.30 | 响应时间 ∈ [0.05, 0.35] s |

### 全部参数清单

`LeafField|General`：`RTSize=256`、`CaptureWidth=500`、`LeafFieldActorTag=LeafField`
`LeafField|Wind` ★：`WindStrength=1.0`、`VerticalLift=0.3`、`MaxWindSpeed=800`
`LeafField|Advanced`：`VelocityScale=600`（与 MaxWindSpeed 配对，别动）、`PushLogIntervalFrames=0`、两个资产路径

---

## 7. 性能笔记（手机端）

- ✅ RT 256×256 RGBA16f（已是默认）
- ✅ `PushLogIntervalFrames=0`（已是默认，上线必须关）
- ✅ NiagaraComponent 已缓存，不每帧 TActorIterator
- ✅ HLSL 扰动仅 4×frac+4×sin，per-particle 可接受
- 粒子数控制在 200 以内（SampleTexture2D 是 per-particle GPU 开销）

---

## 8. 排查 Checklist

| 现象 | 排查点 |
|---|---|
| Niagara `VelocityRT` 一直是 None | NiagaraActor 没加 Tag `LeafField` / User Parameter 名字不对（必须 `User.` 前缀） |
| 主角动但叶子完全不动 | Source 组件没挂到角色 / 没进 BeginPlay |
| 静止时叶子持续往 +X+Y 飘 | HLSL 忘了 `-0.5` |
| 方向对但速度只有一半 | HLSL 忘了 `*2` |
| **只有 +X+Y 能吹，-X-Y 无反应** | `M_FluidSplat` 的 Material Domain 不是 Surface（必须改） |
| 切预乘 Alpha 后还是不行 | gauss weight 节点没接 Opacity |
| RT 预览全黑 | 资产 ClearColor 不是 (0.5,0.5,0,1) |
| 叶子追风太黏 | 调小 HLSL `RespMin` 或调大 `WindStrength` |
| 叶子被瞬移到风速 | `RespMin` 太小接近 DeltaTime / `WindStrength` 过大 |
| 编译报 Niagara 找不到 | `Leaves.Build.cs` 缺 `"Niagara"`/`"RenderCore"` |

> 调试技巧：HLSL 里临时 `OutVel = float3(500,0,0)` 确认输出链路，`Alpha=1.0` 锁定到 RT 值确认采样链路。

---

## 9. 如果以后要加尾流

恢复 v2 双 RT 形态：
1. 加回 `RT_B` / `bCurrentIsA` / `AdvectMaterial*` / `Dissipation`
2. 加回 `AdvectPass`：读 Src，按 `UV' = UV - V*dt/CaptureWidth` 回溯采样、`*DissipationFrame` 写 Dst
3. Tick 改成 `Advect + Splat + Swap`
4. **Advect 材质同样必须 Surface Domain**，采样后先解码 `(S-0.5)*2`、处理完再编码 `*0.5+0.5` 写回，Blend Mode 用 **Opaque**（平流是覆盖写）
5. 当前 Splat 材质（Surface + AlphaComposite + Opacity）完全兼容
