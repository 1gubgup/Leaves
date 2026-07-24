# 地面落叶 Niagara 方案

两个完全独立的落叶方案，共用同一套"区域撒点 + 角色踢起"的基础逻辑，**核心区别只有两点**：

| | 方案 A｜隐藏落叶并固定 | 方案 B｜显示落叶并保持 |
| --- | --- | --- |
| 平时状态 | 藏在地下，看不见 | 铺在地面，一直可见 |
| 被踢起落回后 | 沉回地下、重置，可再次触发 | 贴合地形冻结，永久留在原地 |
| 自定义模块 | Spawn → Initialize → Interaction → **Recycle**（4 个） | Spawn → Interaction → **Grounding**（3 个） |
| 状态机字段 | 有（HomePosition / HomeOrientation / IsResting / GroundInitialized） | 无 |

---

## 共同基础

两个发射器结构一致，只是自定义模块不同：

- **发射器**：`Emitter State = Self / Once`，`Spawn Burst Instantaneous` 一次性铺满，不循环生成。
- **通用力**（顺序相同）：`Gravity Force` → `Aerodynamic Drag` → …交互… → `Solve Forces and Velocity` → `Align Sprite to Mesh Orientation`。
- **用户参数**（完全一致）：

  | 参数 | 含义 |
  | --- | --- |
  | 影响范围 `InfluenceRange` | 角色多近才踢得动叶子（半径 = 50 × 该值） |
  | 抬升速度 `LiftSpeed` | 叶子被踢起的上扬速度系数 |
  | 落叶区域中心 `FieldOrigin` | 撒点区域中心点 |
  | 落叶区域尺寸 `FieldExtent` | 区域长宽（默认 200 × 200） |
  | 落叶区域旋转角度 `FieldRotation` | 区域绕 Z 轴旋转 |
  | 落叶数量 | 一次铺多少片（默认 100） |

- **共有的两个自定义模块**：
  - **Spawn**：在旋转后的矩形区域内随机撒点，并给每片叶子一个"平躺 + 随机绕 Z 旋转"的朝向。
  - **Interaction**：读角色速度和位置，角色靠近时按距离衰减把叶子踢起来。

下面分别说明两个方案在此基础上的差异。

---

## 方案 A｜隐藏落叶并固定

> 叶子平时藏在地下看不见，玩家走过才"冒出来"飞起，落回地面后重新沉入地下并重置——可被无限次触发。

模块链：**Spawn → Initialize → Interaction → Recycle**

### 1. Spawn（生成 + 初始状态）
在区域内随机撒点、设定平躺朝向，并额外记录一整套**状态字段**，供后续模块驱动：

```hlsl
OutHomePosition    = spawnPosition;   // 记住老家位置
OutHomeOrientation = spawnOrientation;// 记住老家朝向
OutIsResting       = true;            // 初始休眠
OutGroundInitialized = false;         // 尚未贴地
```

### 2. Initialize（藏到地下，只跑一次）
用 `Landscape.GetHeight` 采样地面高度，把叶子的"家"下沉到**地面下方 5 单位**，因此平时完全看不见：

```hlsl
if (GroundInitialized) return;            // 已处理则跳过
Landscape.GetHeight(Position, sampledHeight, heightValid);
// 距离过远或无效则不处理
float3 hiddenPosition = float3(Position.xy, sampledHeight - 5.0);
OutPosition = OutHomePosition = hiddenPosition;
OutGroundInitialized = true;
```

### 3. Interaction（角色踢起）
角色进入影响半径时，按距离衰减 + 逐叶固定随机值给出上扬和横向速度，并**解除休眠**：

```hlsl
float targetVelocityZ = playerSpeed * LiftSpeed * influence;
if (targetVelocityZ > Velocity.z)
{
    OutVelocity.xy = playerVelocity * influence * (0.5 + rand * 0.5);
    OutVelocity.z  = targetVelocityZ;
    OutRotationalVelocity = rand.yxw * 5.0 * influence;
    OutIsResting = false;                 // 激活
}
```

### 4. Recycle（钉住 / 回收重置）
- **休眠或未初始化**的叶子：强制钉回老家（位置、朝向归位，速度清零）——所以静止时它就乖乖埋在地下。
- 飞起来的叶子**再次落回地下**（低于老家 50 单位）时：**回收重置**为休眠状态，等待下一次被踢起。

```hlsl
if (!GroundInitialized || IsResting) { /* 钉回 HomePosition */ return; }
if (Position.z < HomePosition.z - 50.0) { /* 归位并 OutIsResting = true */ }
```

**效果**：一块看不见的"落叶埋伏区"，玩家一走过就扬起一片叶子，随后归于沉寂，可反复触发。

---

## 方案 B｜显示落叶并保持

> 叶子一开始就铺在地面上、始终可见，玩家走过把它踢起，落下时**贴合地形法线并冻结**，永久留在新位置。

模块链：**Spawn → Interaction → Grounding**

### 1. Spawn（生成，无状态字段）
同样在旋转区域内随机撒点、设定平躺随机朝向，但**不记录任何状态字段**，结构更轻：

```hlsl
OutPosition    = float3(FieldOrigin.xy + rotatedOffset, FieldOrigin.z);
OutOrientation = float4(0.0, 0.0, leafSin, leafCos);  // 平躺 + 随机绕 Z
OutVelocity = OutRotationalVelocity = 0;
```

### 2. Interaction（角色踢起）
逻辑与方案 A 相同（距离衰减 + 逐叶随机上扬），但因为没有状态机，**不涉及 IsResting**：

```hlsl
OutVelocity.z = playerSpeed * LiftSpeed * influence;
if (OutVelocity.z > Velocity.z)
{
    OutVelocity.xy = playerVelocity * influence * (0.5 + rand * 0.5);
    OutRotationalVelocity = rand.yxw * 5.0 * influence;
}
```

### 3. Grounding（贴地 + 冻结，方案 B 的核心）
这是与方案 A 最大的不同——不回收，而是让叶子**顺着地形躺平并停住**：

1. **采样地面**：`GetHeight` 取高度，落点在 `groundZ = 地面 + 5`。
2. **贴合权重**：只有"正在下落 + 接近地面"时才开始对齐（`blendWeight` 由高度差和向下速度决定）。
3. **对齐地形法线**：`GetWorldNormal` 取地面法线，用四元数 `swing` 把叶子朝向摆到与坡面一致，随高度平滑 `lerp` 过渡。
4. **防穿地**：`OutPosition.z = max(Position.z, groundZ)`，并随贴合程度降低旋转速度。
5. **落地冻结**：一旦触地且不再上升，位置/朝向锁定、速度清零，**永久停在原地**：

```hlsl
if (Position.z <= groundZ && Velocity.z <= 0.0)
{
    OutPosition = float3(Position.xy, groundZ);
    OutVelocity = OutRotationalVelocity = 0;
    OutOrientation = targetOrientation;   // 冻结为贴合地形的朝向
}
```

**效果**：一片始终可见的地面落叶，玩家踩过会被扬起，随后重新贴着地形落定、保留被踢散后的新分布，形成可累积的"踩踏痕迹"。

---

## 一句话选型

- **要"可反复触发、平时干净不留痕"** → 方案 A（隐藏落叶并固定）。
- **要"落叶一直在、踩过留下真实分布"** → 方案 B（显示落叶并保持）。
