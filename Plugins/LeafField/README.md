# LeafField 插件

角色移动速度实时写入全局速度场 RT，Niagara 粒子通过 WindInteraction 模块读取并产生交互效果。

---

## 快速上手

### 1. 给角色添加组件

在角色蓝图的组件列表中添加 `LeafInteractionSource` 组件，无需任何额外配置。

### 2. 给 Niagara 添加模块

打开目标 Niagara System，在 **Solve Forces and Velocity** 之前插入 `WindInteraction` 模块。

将模块中 `VelocityRT` 的纹理设置为 `RT_VelocityField`。

### 3. 运行

运行后操控角色走过粒子区域，即可看到交互效果。

---

## 参数说明

### 角色组件（LeafInteractionSource）

选中组件后切换到「所有」Tab，`LeafField` 分类下可见：

| 参数 | 默认值 | 怎么调 |
|------|--------|--------|
| `BrushRadiusWorld` | 200 cm | 角色周围多大范围的粒子会被影响。觉得影响范围太小就调大，太大就调小 |
| `VelocityStrength` | 1.0 | 角色移动对粒子的推力。觉得叶子被吹得不够猛就调大，太夸张就调小 |
| `VelocityDecayTime` | 0.1 s | 角色停步后推力消失的快慢。0 = 停步立即没风；调大（如 0.5）= 停步后有"余风"慢慢消散 |
| `bUsePeakHold` | true | 保持 true 即可。改为 false 后角色起步时会有迟钝感 |

### Niagara 模块（WindInteraction）

| 参数 | 默认值 | 怎么调 |
|------|--------|--------|
| `WindStrength` | 1.0 | 粒子整体被吹动的强度。觉得粒子反应太弱调大，太飘调小 |
| `WindLift` | 0.1 | 粒子被吹起时向上飘的程度。0 = 完全平移；调大（如 0.3）= 粒子会被明显吹起来 |
| `WindResponseSpeed` | 5.0 | 粒子跟上风场的快慢。调大 = 反应灵敏，调小 = 反应迟缓柔和 |
| `WindSpinScale` | 0.5 | 粒子在风中的旋转速度。1.0 = 不变；< 1 = 被吹时旋转变慢；> 1 = 被吹时旋转加速 |

### 项目设置（项目设置 → 插件 → Leaf Field）

改动后需重启 PIE 生效，通常不需要动。

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `VelocityFieldWidth` | 1000 cm | 速度场覆盖的范围，超出范围的角色不会产生影响 |
| `VelocityFieldRTSize` | 256 px | 速度场精度，通常 256 够用 |
| `WindMaxSpeed` | 1000 cm/s | **勿修改**，改了会导致风向错乱 |
