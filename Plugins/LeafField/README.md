# LeafField 地面落叶插件 · 美术使用手册

把 `ALeafInteractionField` 拖入关卡，设置好叶片网格，叶子就会自动铺满指定区域，
角色走过去叶子会被实时扇起，落下后重新贴地。

> 如需了解底层实现，见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

---

## 第一步：接入准备（只需做一次）

### 1. 给主角挂载"扰动源"组件

打开**主角的蓝图**（角色或 Pawn），在 Components 面板点 `Add Component`，搜索并添加 `LeafInteractionSource`。

参数保持默认即可，**不挂这个组件叶子就不会被任何角色扰动**。

### 2. 把 Field 拖入关卡

在内容浏览器找到 `ALeafInteractionField`（或其蓝图子类），**拖到关卡里你想铺叶子的地方**，尽量把 Actor 原点放在地面上。

### 3. 填入必填项（不填叶子出不来）

选中 Field，在 Details 面板找到 **Asset** 分组：

| 必填项 | 填什么 |
|--------|--------|
| `Leaf System` | 选 `N_LeafField` |
| `Leaf Meshes` | 至少加 1 项，选一个叶片 Static Mesh |

填完之后 `bAutoActivateOnBeginPlay` 默认勾选，**Play 之后叶子会自动出现**，不需要额外操作。

---

## 第二步：调效果（按顺序来）

建议按以下顺序调，先保证叶子位置对，再调数量，最后调风感。

---

### 第 1 步：确认叶子位置正确

**目标**：叶子贴在地面上，不穿地、不悬空。

| 参数 | 在哪里 | 默认值 | 怎么调 |
|------|--------|--------|--------|
| `Height Capture ZOffset` | Advanced | 2000 cm | 如果叶子全部悬空或不出现，把这个值**调高**，必须高于场地内最高的地形（单位 cm，20m 以内的平坦地形默认值够用） |
| `Ground Offset` | Appearance | 5 cm | 叶子稍微穿地就调大（比如 8~10 cm），悬空太高就调小 |

> **常见问题**：叶子全部出现在同一个奇怪位置 → `Height Capture ZOffset` 太小，相机嵌入地面了，调大即可。

---

### 第 2 步：调整铺叶范围和数量

| 参数 | 在哪里 | 默认值 | 怎么调 |
|------|--------|--------|--------|
| `Field Extent` (X, Y) | Layout | 500, 500 cm | 叶子铺设的**半径**。500 = 以 Actor 为中心铺 10m×10m 的区域。直接拉大/缩小即可 |
| `Field Extent` (Z) | Layout | 10 cm | 只影响编辑器里的黄框高度，不影响叶子，保持默认就行 |
| `Leaf Count` | Appearance | 1024 | 叶子总数量。太稀疏就调大，太密就调小。**改完需要重新 Play 才生效**（数量是 Spawn 时决定的） |

> 提示：叶子数量和性能直接相关，建议从 1024 开始，按需增减。

---

### 第 3 步：添加多种叶子混撒

在 `Leaf Meshes` 列表里**加多项**，每项填一个不同的叶片 Mesh。

`Weight`（权重）决定各种叶子出现的比例，**不需要手动换算**，直接填整数即可：

| 槽位 | Mesh | Weight | 出现概率 |
|------|------|--------|----------|
| 0 | 枫叶 | 2 | 50% |
| 1 | 银杏叶 | 1 | 25% |
| 2 | 落叶 | 1 | 25% |

留空的槽位（不填 Mesh）会自动忽略，权重锁为 0。

---

### 第 4 步：调风感

Wind 分组下的参数，**建议先调 WindStrength 确定整体强度，再细调其他参数**。

| 参数 | 默认值 | 效果说明 | 调参建议 |
|------|--------|----------|----------|
| `Wind Strength` | 1.0 | 整体风力强度。0 = 完全无风，3 = 最大风力 | 先用这个参数确定"叶子能被吹起多猛"，之后其他参数在这个基础上微调 |
| `Wind Lift` | 0.05 | 水平风转变为上抬力的比例。0 = 叶子只水平飘，1 = 上抬力等于水平风 | 0.05~0.15 比较自然，调太大叶子会飞得很高 |
| `Wind Response Speed` | 10.0 | 叶子跟上风速的快慢（数值越大跟得越快）| 5 左右有明显拖拽感，10 轻微拖尾，20 几乎即时响应 |
| `Wind Spin Impulse` | 1.0 | 叶子被风"踢起"时翻滚的强度。0 = 不翻滚 | 1.0 比较自然，调大翻滚更剧烈，调到 0.5 左右更轻柔 |

**调 Wind 参数的顺序建议**：
1. `Wind Strength` → 确认叶子能被吹起
2. `Wind Response Speed` → 调整叶子是"即时响应"还是"慢慢跟上"
3. `Wind Lift` → 确认叶子飞起来的高度合适
4. `Wind Spin Impulse` → 最后微调翻滚感

> 小技巧：Details 面板改 Wind 参数时**不需要停止 Play**，改了立刻生效，可以实时对比效果。

---

### 第 5 步（可选）：调角色的扰动范围

选中**主角蓝图里的 `LeafInteractionSource` 组件**，在 Details 面板调整：

| 参数 | 默认值 | 效果说明 |
|------|--------|----------|
| `Brush Radius UV` | 0.35 | 角色周围的扰动半径（速度场范围 1000cm 时约 350cm）。调大影响范围更广 |
| `Velocity Strength` | 1.0 | 这个角色扰动叶子的力度倍率。BOSS 等大型单位可以调到 2~3 |
| `Velocity Decay Time` | 0.25 s | 停步后速度消失的快慢。调小停步更干脆，调大停步后叶子还会惯性飘一会儿 |

---

## 全参数速查表

### Field 上的参数（每个 Field 独立）

| 分类 | 参数 | 默认值 | 说明 |
|------|------|--------|------|
| Asset | `Leaf System` | — | **必填**，指向 N_LeafField |
| Asset | `bAutoActivateOnBeginPlay` | ✓ | 勾选时 Play 自动出叶子，无需蓝图调用 |
| Asset | `Leaf Meshes` | — | **必填**，至少 1 个有效 Mesh |
| Layout | `Field Extent` (XY) | 500, 500 cm | 铺叶半径 |
| Appearance | `Leaf Count` | 1024 | 叶子总数，改完需重新 Play |
| Appearance | `Ground Offset` | 5 cm | 贴地距离，防穿地 |
| Wind | `Wind Strength` | 1.0 | 整体风力（0~3） |
| Wind | `Wind Lift` | 0.05 | 上抬力比例（0~1） |
| Wind | `Wind Response Speed` | 10.0 | 响应速度，越大跟风越快 |
| Wind | `Wind Spin Impulse` | 1.0 | 被吹起时翻滚强度（0~5） |
| Advanced | `Height Capture ZOffset` | 2000 cm | 高度相机拍摄高度，需高于场内最高地形 |
| Advanced | `Ground Blend Height` | 10 cm | 贴地过渡区高度，一般不需要动 |

### 全局参数（项目设置 → 插件 → Leaf Field）

这些参数影响所有 Field，**一般不需要动**，如有特殊需求可咨询程序。

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `Height RT Size` | 256 px | 高度图精度，越大贴地越精细，内存占用越多 |
| `Velocity Field Width` | 1000 cm | 速度场覆盖范围（以玩家为中心） |
| `Velocity Field RT Size` | 256 px | 速度场贴图精度 |
| `Wind Max Speed` | 1000 cm/s | 速度编码基准，**不要修改** |

---

## 故障排查

| 现象 | 解决方法 |
|------|----------|
| **叶子完全不出现** | ① `Leaf System` 是否填了 `N_LeafField`？② `Leaf Meshes` 是否至少有一项有效 Mesh？③ 若关闭了 `bAutoActivateOnBeginPlay`，需要在蓝图里手动调用 `Activate Field` |
| **叶子全部在同一个奇怪位置 / 出现在天空** | `Height Capture ZOffset` 太小，相机嵌入地面。调大到高于场内最高点 |
| **叶子穿地** | 调大 `Ground Offset`（5 → 10 cm） |
| **叶子悬空** | 调小 `Ground Offset`，或调大 `Height Capture ZOffset` |
| **叶子不被角色扰动** | ① 主角蓝图是否挂了 `LeafInteractionSource` 组件？② 角色是否在速度场范围内（默认 1000cm，以玩家为中心） |
| **人停下后叶子还在向一个方向飘** | 速度场清除颜色设置有误，联系程序检查 `RT_VelocityField` 的 ClearColor |
| **改了叶子数量没效果** | `Leaf Count` 需要重新 Play（或重新 Activate Field）才生效 |
| **运行时改了地形叶子没跟上** | 高度图只拍一次，联系程序重置 `bHeightCaptured` 后重新 Play |
