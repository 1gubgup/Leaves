# LeafField

落叶交互风场插件（UE5 源码插件）。角色经过时实时扰动落叶 Niagara 粒子。

---

## 3 步接入

### 1. 启用插件
把 `Plugins/LeafField/` 整个目录拷到自己项目的 `Plugins/` 下，启动编辑器让它编译一次。

### 2. 主角挂组件
打开 **主角的 Pawn / Character 蓝图** → Add Component → **LeafInteractionSource**。默认参数即可用。

C++ 写法：
```cpp
#include "LeafInteractionSourceComponent.h"
LeafInteractionSource = CreateDefaultSubobject<ULeafInteractionSourceComponent>(TEXT("LeafInteractionSource"));
```

### 3. 场景放 Niagara + Tag
在 Content Browser 右下角点 **Settings** 勾选 **Show Plugin Content**，从 `LeafField Content / N_Leaves` 拖进场景。
选中该 Niagara Actor → Details → Actor → **Tags** → 加一项 `LeafField`。

完成。Play 跑动即可看到落叶被扰动。

---

## 调参指南

调参分两层：先想清楚要调"风的力度"还是"粒子的物理感"，再去对应位置。

### A. 风的"力度" → Subsystem 字段（C++ 默认值 / 运行时蓝图）

`ULeafInteractionFieldSubsystem` 是 `UTickableWorldSubsystem`。**它不会出现在 Project Settings 里**，调整方式只有两种：

**方式 1：改 C++ 默认值（持久生效）**
直接改 `LeafInteractionFieldSubsystem.h` 里这几个 `UPROPERTY` 的初值，重新编译：

| 参数 | 默认 | 说明 |
|---|---|---|
| `CaptureWidth` | 500 cm | 风场覆盖范围（以玩家为中心的方形边长） |
| `LeafFieldActorTag` | `LeafField` | 接收风场的 Niagara Actor Tag |
| `WindStrength` | 1.0 | 风强度倍率，更猛 → 1.5~2.5 |
| `VerticalLift` | 0.3 | 水平风 → 上抬比例，更飘 → 0.5~0.8 |
| `MaxWindSpeed` | 800 cm/s | 风对粒子的速度上限，更快 → 1200~1600 |

**方式 2：运行时蓝图改（即时生效，调参 / 风暴效果用）**
```
Get World Subsystem → ULeafInteractionFieldSubsystem
   ├─ Set Wind Strength = 2.5
   ├─ Set Vertical Lift = 0.6
   └─ Set Max Wind Speed = 1200
```
下一帧立即生效。适合做"BOSS 出现 → 风变猛"之类的动态效果。

> ⚠️ Niagara 资产里那 6 个 `User.*` 参数（`CaptureCenter` / `WindStrength` 等）**不要在 Niagara 编辑器里改**——它们每帧都被 Subsystem 覆盖回去，改了等于没改。它们只是"接收槽位"。

### B. 单角色的扰动半径 / 强度 → Source 组件参数

挂在角色 Actor 上的 `LeafInteractionSource` 组件。**直接在组件 Details 面板里调**，编辑器内改完就生效；运行时也可以蓝图读写（`BlueprintReadWrite`）。

| 参数 | 默认 | 说明 |
|---|---|---|
| `BrushRadiusUV` | 0.35 | 笔刷大小（UV 空间，0.01~0.8）。`CaptureWidth=500cm` 时：0.2≈100cm、0.35≈175cm、0.5≈250cm 半径 |
| `VelocityStrength` | 1.0 | 该角色的速度倍率。BOSS 想更"炸" → 2~3；潜行 → 0.3 |
| `VelocityDecayTime` | 0.25 秒 | 停下后速度的衰减时间常数。0 = 即停即停（瞬时）；0.6~1.0 = 明显风过留香 |
| `bUsePeakHold` | true | 推荐保持 true：起步零延迟 + 停步柔和衰减。false = 纯低通，起步也会稍延迟 |

### C. 粒子的"物理感" → Niagara 资产

打开 `LeafField Content / N_Leaves`，Stack 模块都是 UE 标准模块或本插件自定义模块，按需修改保存即生效：

| 想改什么 | 改哪个 module |
|---|---|
| 落叶**数量** | `Spawn Rate` 或 `Spawn Burst Instantaneous` |
| 落叶**生命周期** | `Particle State` 的 Lifetime |
| **重力** | `Gravity Force` / `Acceleration Force` |
| **空气阻力** | `Drag` |
| **风场采样**（受角色扰动的强弱） | `LF_SampleWindField`（自定义 Scratch 模块） |
| **地面碰撞** / 地面高度 | `LF_GroundCollision`（自定义 Scratch 模块） |
| 落叶**纹理 / 形状** | Render 阶段 `Sprite Renderer` 的 Material |
| 初始**散布范围** | `Shape Location` |

> 想做完全不同风格的落叶（比如雪 / 樱花瓣）？复制 `N_Leaves` 改一份就行，不用动 C++。

---

## 资产清单

插件内置以下资产（位于 `Plugins/LeafField/Content/`）：

| 资产 | 路径 | 作用 |
|---|---|---|
| `N_Leaves` | `/LeafField/N_Leaves` | 落叶 Niagara 系统（拖进场景使用） |
| `RT_VelocityField` | `/LeafField/LeafField/RT_VelocityField` | 速度场 RenderTarget（C++ 自动加载） |
| `M_FluidSplat` | `/LeafField/LeafField/M_FluidSplat` | 速度笔刷材质（C++ 自动加载） |
| `Fab/...` | `/LeafField/Fab/...` | `N_Leaves` 依赖的落叶模型与材质 |

> ⚠️ `RT_VelocityField` 和 `M_FluidSplat` 的资产路径**写死在 `LeafInteractionFieldSubsystem.cpp` 顶部**，不要重命名也不要移动子目录；如确需修改，请同步改 `VelocityRTAssetPath` / `SplatMaterialPath` 两个常量并重新编译。

---

## 故障排查

| 现象 | 检查 |
|---|---|
| 落叶完全不动 | ① Niagara Actor 是否加了 Tag `LeafField`（注意大小写）；② 主角是否真的挂上了 `LeafInteractionSource` 组件；③ 主角是否在风场范围内（`CaptureWidth` 默认 500cm 即玩家周围 5m 见方）。 |
| 人停下后落叶往一个固定方向飘 | 这是早期遇到过的 bug：`RT_VelocityField` 的 ClearColor 必须是 `(0.5, 0.5, 0, 1)`（编码后的"零速度"）。代码里 `Initialize` 已自动设置，但若你手改过资产，请把 ClearColor 还原。 |
| 落叶只在世界原点附近响应 | `CaptureCenter` 没跟随主角。代码里取的是 `PlayerController(0)->GetPawn()->GetActorLocation()`——确认场景里有合法的本地玩家 Pawn。 |
| 落叶穿过地面 | 进 `N_Leaves` 改 `LF_GroundCollision` 模块的 `GroundZ` 常量（默认 0）。该模块只做 Z 平面检测，不查物理世界。 |

---

## 实现原理

整套交互的本质：**"角色速度 → 速度场 RT → Niagara 采样 → 粒子受力"**。全程不写自定义 GPU 计算着色器，纯 Canvas + 标准 Niagara 模块，便于排查与移植。

### 数据流（每帧）

```
[Source 组件 Tick]                [Subsystem Tick]                       [N_Leaves 粒子]
位置差分 → 衰减/峰值保持 ─────►   1) 跟随主角 → CaptureCenter
GetVelocityXY()                   2) ClearRT → 编码零 (0.5,0.5,0,1)
                                  3) SplatPass: Canvas 画笔刷 ────────►  4) LF_SampleWindField
                                  4) PushToNiagara: SetVar 6 个参数         按粒子位置算 UV
                                                                            采样 RG 解码 → 风速
                                                                            lerp 到粒子 Velocity
                                                                          5) LF_GroundCollision
                                                                            Z≤GroundZ → 贴地清零
```

### 关键模块

1. **`ULeafInteractionSourceComponent`（角色侧）**
   - 用 Owner 位置差分计算瞬时速度（无需依赖 Movement Component）。
   - 用 `Tau = VelocityDecayTime` 的指数衰减做平滑：`K = exp(-dt/Tau)`，与帧率无关。
   - `bUsePeakHold = true` 时：本帧瞬时速度若大于衰减后的缓存值则直接覆盖——做到**起步零延迟、停步柔和拖尾**。
   - BeginPlay 自动 `RegisterSource`，EndPlay 自动 `UnregisterSource`。

2. **`ULeafInteractionFieldSubsystem`（World Subsystem）**
   - 持有一张 `RTSize × RTSize` 的 RenderTarget（**`RTSize = 128`**，写死在 .h 里；格式以资产 `RT_VelocityField` 为准，当前为 RG8）。优先 `LoadObject` 加载磁盘资产；若加载失败则 fallback 到 `CreateRenderTarget2D` 动态新建一张同尺寸 RG8。
   - **限频更新**：内部常量 `UpdateRateHz = 30.f`（写死在 .h 里），用累加器节流——风场 30Hz 更新即可，Niagara 端的 lerp 会平滑掉视觉差。设 0 则每帧更新。
   - **速度归一化**：`VelocityScale = 600 cm/s`，Splat 前 `NormVel = SourceVel / VelocityScale` 后映射到 RG8。源端的速度若超过 600，材质里 `*0.5+0.5` 会越界，由 Niagara 端的 `length(NormVel)>1` 钳到单位长度兜底。
   - 每帧 Tick 顺序：限频检查 → 跟随主角 → `ClearRenderTarget2D` 清成编码零 `(0.5, 0.5, 0, 1)` → Splat 阶段画笔刷 → Push 阶段把 RT/参数推给所有 Niagara 组件。
   - `CaptureCenter` 取本地 PlayerController 的 Pawn 位置，所以风场是 **以玩家为中心的滑动窗口**——Source 的 UV 落在 `[-0.1, 1.1]` 之外（约等于 ±10% × CaptureWidth 边距）就跳过 Splat，不浪费像素。
   - Niagara 组件采用 **lazy 缓存 + `OnActorSpawned` 增量维护**：第一次 `PushToNiagara` 缓存为空时全场扫一次，之后新生成的带 Tag Actor 由 spawn 回调自动加入，避免每帧 `TActorIterator` 扫场。

3. **`LF_SampleWindField`（Niagara 自定义 Scratch 模块）**
   > ⚠️ 以下是**模块的设计意图**说明，不是从 `.uasset` 中逐节点核对得到的——若你重新编辑过 Scratch 内容，以编辑器内的实际节点图为准。
   - 把粒子世界坐标换算成 UV：`UV = (InPos.xy - (Center.xy - Width/2)) / Width`。
   - 仅当 UV ∈ [0,1]² 时进入风场处理；否则保持原速度。
   - 采样 RT 的 RG 通道并解码：`NormVel = (Sample.rg - 0.5) * 2`，必要时把长度钳到 1（防编码端越界）。
   - 构造目标风速：水平分量 ≈ `NormVel * MaxWindSpeed * WindStrength`，垂直分量 ≈ `|水平| * VerticalLift`；这三个 User Parameter 由 C++ 每帧 push。
   - 输出方式为 **lerp 替换**：`OutVel = lerp(InVel, WindVel, Alpha)`——风场内粒子的速度被风"接管"，不是叠加。

4. **`LF_GroundCollision`（Niagara 自定义 Scratch 模块）**
   > ⚠️ 同上，是设计意图描述，未从 `.uasset` 节点图核对。
   - 判定 `Position.z <= GroundZ`（`GroundZ` 在模块内是常量，默认 0）。
   - 落地后把粒子贴地：`Velocity / RotationalVelocity` 清零，`Position.z = GroundZ`。**不反弹**。
   - 不做 trace、不查物理世界，仅一次标量比较，开销几乎为零。仅适合平地；非平地需要把 `GroundZ` 改成实际地面 Z 值。

### Niagara User Parameter 数据流

`N_Leaves` 暴露 6 个 `User.*` 参数：`VelocityRT` / `CaptureCenter` / `CaptureWidth` / `WindStrength` / `VerticalLift` / `MaxWindSpeed`。它们的值由 `PushToNiagara()` 每帧调用 `SetTextureObject` / `SetVariableVec3` / `SetVariableFloat` 写入。**这是 C++ → Niagara 的单向推送**，所以在 Niagara 编辑器里改这 6 个的默认值不会有运行时效果。
