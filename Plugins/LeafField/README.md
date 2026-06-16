# LeafField 插件

角色移动速度实时写入全局速度场 RT，Niagara 粒子通过 WindInteraction 模块读取并产生交互效果。

---

## 安装插件

1. 将 `LeafField` 文件夹复制到项目的 `Plugins/` 目录下
2. 打开项目，弹出"是否重新编译插件"时点击**是**
3. 编译完成后，在编辑器菜单 **编辑 → 插件** 中搜索 `LeafField`，确认已启用

---

## 快速上手

### 第一步：给角色添加组件

打开角色蓝图，在组件列表中点击「添加」，搜索并添加 `Leaf Interaction Source` 组件，无需任何额外配置。

### 第二步：给 Niagara 添加模块

1. 打开目标 Niagara System
2. 在 **Solve Forces and Velocity** 之前插入模块：
   - 模块路径：`Plugins/LeafField Content/LeafField/WindInteraction`
3. 模块添加后，将 `VelocityRT` 槽位的纹理拖入：
   - 纹理路径：`Plugins/LeafField Content/LeafField/RT_VelocityField`

### 第三步：运行

Play 后操控角色走过粒子区域，即可看到交互效果。

---

## 插件内容资产路径

| 资产 | 路径 |
|------|------|
| `RT_VelocityField` | `Plugins/LeafField Content/LeafField/RT_VelocityField` |
| `WindInteraction`（Niagara 模块） | `Plugins/LeafField Content/LeafField/WindInteraction` |
| `NPC_LeafField`（Niagara 参数集） | `Plugins/LeafField Content/LeafField/NPC_LeafField` |
| `M_FluidSplat`（速度场写入材质） | `Plugins/LeafField Content/LeafField/M_FluidSplat` |

> 在内容浏览器左上角勾选「显示插件内容」后可看到上述路径。

---

## 参数说明

### 角色组件（Leaf Interaction Source）

选中组件后切换到「所有」Tab，`LeafField` 分类下可见：

| 参数 | 默认值 | 怎么调 |
|------|--------|--------|
| `BrushRadiusWorld` | 200 cm | 角色影响周围粒子的范围。觉得影响范围太小就调大，太大就调小 |
| `VelocityStrength` | 1.0 | 角色移动对粒子的推力。叶子被吹得不够猛就调大，太夸张就调小 |
| `VelocityDecayTime` | 0.1 s | 角色停步后推力消散的时间。0 = 停步立即没风；调大（如 0.5）= 停步后有余风慢慢消散 |
| `bUsePeakHold` | true | 保持 true 即可。改为 false 后角色起步时会有迟钝感 |

### Niagara 模块（WindInteraction）

| 参数 | 默认值 | 怎么调 |
|------|--------|--------|
| `WindStrength` | 1.0 | 粒子整体被吹动的强度。粒子反应太弱调大，太飘调小 |
| `WindLift` | 0.1 | 粒子被吹起时向上飘的程度。0 = 水平吹动不上飘；调大（如 0.3）= 粒子会被吹起来 |
| `WindResponseSpeed` | 5.0 | 粒子跟上风场的快慢。调大 = 反应灵敏，调小 = 反应迟缓柔和 |
| `WindSpinScale` | 0.5 | 粒子在风中的旋转速度。1.0 = 不变；小于 1 = 旋转变慢；大于 1 = 旋转加速 |

### 项目设置（编辑 → 项目设置 → 插件 → Leaf Field）

改动后需重启 PIE 生效，通常不需要动。

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `VelocityFieldWidth` | 1000 cm | 速度场覆盖的范围（以玩家为中心的正方形区域），超出范围的角色不会产生影响 |
| `VelocityFieldRTSize` | 256 px | 速度场精度，通常 256 够用 |
| `WindMaxSpeed` | 1000 cm/s | **勿修改**，改了会导致风向错乱 |
