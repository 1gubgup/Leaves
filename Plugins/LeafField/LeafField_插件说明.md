# LeafField 插件说明

角色移动时，`LeafInteractionSource` 组件将速度写入全局速度场 RT（`RT_VelocityField`），Niagara 粒子通过 `WindInteraction` 模块采样该 RT，产生随角色移动而扰动的交互效果。

---

## 使用步骤

**第一步：给角色添加组件**

在角色蓝图的组件列表中添加 `Leaf Interaction Source`，无需其他配置，运行时自动生效。

**第二步：给 Niagara 添加模块**

打开目标 Niagara System，在 **Solve Forces and Velocity** 之前插入 `WindInteraction` 模块。

将模块中 `VelocityRT` 槽位的纹理选择为 `RT_VelocityField`。

**第三步：运行**

Play 后操控角色走过粒子区域，即可看到粒子对角色移动产生响应。

---

## 可调参数

### 组件参数（挂在角色身上）

在角色蓝图中选中 `Leaf Interaction Source` 组件，切换到「所有」Tab，`LeafField` 分类下可见：

| 参数 | 默认值 | 怎么调 |
|------|--------|--------|
| `BrushRadiusWorld` | 200 cm | 角色周围多大范围的粒子会被影响。觉得影响范围太小就调大，太大就调小 |
| `VelocityStrength` | 1.0 | 角色移动对粒子的推力。觉得叶子被吹得不够猛就调大，太夸张就调小 |
| `VelocityDecayTime` | 0.1 s | 角色停步后，推力消失的快慢。0 = 停步立即没有风；调大（如 0.5）= 停步后风力慢慢消散，有"余风"感 |
| `bUsePeakHold` | true | 保持 true 即可。改为 false 后角色起步时会有一点迟钝感 |

### 模块参数（在 Niagara 模块里调）

在 Niagara System 中选中 `WindInteraction` 模块可见：

| 参数 | 默认值 | 怎么调 |
|------|--------|--------|
| `WindStrength` | 1.0 | 粒子整体被吹动的强度。觉得粒子反应太弱调大，太飘调小 |
| `WindLift` | 0.1 | 粒子被吹起时向上飘的程度。0 = 完全平移不上飘；调大（如 0.3）= 粒子会被明显吹起来 |
| `WindResponseSpeed` | 5.0 | 粒子速度跟上风场的快慢。调大 = 粒子反应更灵敏，调小 = 粒子反应更迟缓柔和 |
| `WindSpinScale` | 0.5 | 粒子在风中的旋转速度。1.0 = 旋转不变；小于 1 = 被吹时旋转变慢；大于 1 = 被吹时旋转加速 |

### 全局设置（项目设置 → 插件 → Leaf Field）

改动后需重启 PIE 生效。通常不需要动。

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `VelocityFieldWidth` | 1000 cm | 速度场覆盖的范围（以玩家为中心），超出范围的角色不会产生影响 |
| `VelocityFieldRTSize` | 256 px | 速度场精度，通常 256 够用 |
| `WindMaxSpeed` | 1000 cm/s | **勿修改**，编解码基准值，改了会导致风向错乱 |
