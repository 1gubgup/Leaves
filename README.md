# LeafField 落叶交互风场

角色经过时实时扰动落叶 Niagara 粒子的 UE5 源码插件。

---

## 接入步骤

### 1. 启用插件
将 `Plugins/LeafField/` 整个目录拷贝至项目 `Plugins/` 下，重启编辑器并完成编译。

### 2. 主角挂载组件
打开主角的 Character / Pawn 蓝图 → `Add Component` → 添加 **LeafInteractionSource**。使用默认参数即可。

### 3. 场景中放置落叶
1. Content Browser 右下角 **Settings** → 勾选 **Show Plugin Content**。
2. 在 `LeafField Content` 中将 **`N_Leaves`** 拖入场景。
3. 选中该 Niagara Actor → Details → **Tags** → 添加一项 **`LeafField`**（区分大小写）。

完成后进入 Play 即可看到落叶被扰动。

---

## 调整效果

主要调节参数集中在以下三处。

### A. 单个角色的扰动范围 —— `LeafInteractionSource` 组件

选中挂载组件的角色，在组件 Details 面板中调整：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `Brush Radius UV` | 0.35 | 该角色扰动落叶的半径。0.35 约 1.7m，0.5 约 2.5m |
| `Velocity Strength` | 1.0 | 该角色的扰动强度倍率。BOSS 等大型单位可调至 2~3 |

其他参数（`Velocity Decay Time`、`bUsePeakHold`）保持默认即可，用于精细调节起步与停步时的拖尾手感。

### B. 落叶本体的物理与外观 —— `N_Leaves` Niagara 资产

双击打开 `N_Leaves`，在对应模块中调整：

| 想调整的效果 | 修改位置 |
|---|---|
| 落叶数量 | `Spawn Rate` |
| 落叶生命周期 | `Particle State` 的 Lifetime |
| 重力大小 | `Gravity Force` |
| 空气阻力 | `Drag` |
| 落叶贴图 / 颜色 | Render 阶段 `Sprite Renderer` 的 Material |
| 初始散布范围 | `Shape Location` |
| 落叶地面高度 | `LF_GroundCollision` 的 `GroundZ`（默认 0） |

如需做完全不同风格的落叶（樱花、雪花等），复制 `N_Leaves` 修改副本即可，无需改动 C++。

### C. 风场整体强度 —— C++ 默认值或运行时蓝图

风场总控参数定义在 `ULeafInteractionFieldSubsystem` 中，主要调节项：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `WindStrength` | 1.0 | 风强度倍率。更猛 → 1.5~2.5；更轻柔 → 0.3~0.6 |
| `VerticalLift` | 0.5 | 水平风转化为上抬力的比例。让落叶飘得更高 → 0.5~0.8 |
| `MaxWindSpeed` | 800 | 风对粒子的速度上限（cm/s）。让落叶被吹得更快 → 1200~1600 |

调整方式有两种：

**方式 1：修改 C++ 默认值（持久生效）**
编辑 `LeafInteractionFieldSubsystem.h` 中对应字段的初值，重新编译。

**方式 2：运行时蓝图修改（即时生效）**
适用于 BOSS 出场风变猛、剧情阶段风变弱等动态效果：

```
Get World Subsystem (LeafInteractionFieldSubsystem) → Set Wind Strength / Set Vertical Lift / Set Max Wind Speed
```

下一帧立即生效。

---

## 故障排查

| 现象 | 检查项 |
|---|---|
| 落叶完全不动 | ① Niagara Actor 是否添加了 Tag = `LeafField`（区分大小写）<br>② 主角是否挂载了 `LeafInteractionSource` 组件<br>③ 主角是否在 Niagara Actor 周围 5m 范围内 |
| 落叶穿过地面 | 打开 `N_Leaves` → `LF_GroundCollision` 模块 → 将 `GroundZ` 改为实际地面高度 |
| 人停下后落叶向固定方向飘 | `RT_VelocityField` 的 ClearColor 必须为 `(0.5, 0.5, 0, 1)`，若被误改请还原 |
| 落叶仅在世界原点附近响应 | 风场以本地玩家 Pawn 为中心，确认场景中存在合法的 PlayerController 与 Pawn |

---

## 资产清单

| 资产 | 用途 |
|---|---|
| `N_Leaves` | 落叶 Niagara 系统，需拖入场景使用 |
| `RT_VelocityField` | 速度场 RenderTarget，C++ 自动加载 |
| `M_FluidSplat` | 速度笔刷材质，C++ 自动加载 |

`RT_VelocityField` 与 `M_FluidSplat` 的资产路径写死在代码中，不要重命名或移动。

---

## 工作原理

角色移动产生的速度被采集到一张以玩家为中心的速度场贴图中，Niagara 粒子按自身位置采样该贴图获得风速，从而被实时扰动。
