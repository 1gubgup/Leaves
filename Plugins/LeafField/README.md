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
   - 模块路径：`Plugins/LeafField Content/LeafField/Wind_Interaction`
3. 模块添加后，将 `VelocityRT` 槽位的纹理拖入：
   - 纹理路径：`Plugins/LeafField Content/LeafField/RT_VelocityField`

### 第三步：运行

Play 后操控角色走过粒子区域，即可看到交互效果。

---

## 插件内容资产路径

| 资产 | 路径 |
|------|------|
| `RT_VelocityField`（速度场纹理） | `Plugins/LeafField Content/LeafField/RT_VelocityField` |
| `Wind_Interaction`（Niagara 模块） | `Plugins/LeafField Content/LeafField/Wind_Interaction` |
| `NPC_LeafField`（Niagara 参数集） | `Plugins/LeafField Content/LeafField/NPC_LeafField` |
| `M_FluidSplat`（速度场写入材质） | `Plugins/LeafField Content/LeafField/M_FluidSplat` |

---

## 参数说明

### 角色组件（Leaf Interaction Source）

选中组件后，`LeafField` 分类下可见：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `BrushRadiusWorld` | 200 cm | 速度写入的影响半径，越大扰动范围越广 |
| `VelocityStrength` | 1.0 | 速度写入倍率，值越大粒子被推动得越猛烈 |
| `VelocityDecayTime` | 0.1 s | 停步后速度衰减时间。0 = 立即归零；增大则停步后有余风拖尾效果 |
| `bUsePeakHold` | true | 建议保持 true。起步时推力立即生效，停步时柔和消散；false 则起步也有延迟 |

### Niagara 模块（WindInteraction）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `WindStrength` | 1.0 | 粒子响应风场的整体强度 |
| `WindLift` | 0.1 | 粒子受风时向上飘起的强度。0 = 纯水平运动，增大则粒子会被吹起 |
| `WindResponseSpeed` | 5.0 | 粒子速度跟随风场的响应速度。值越大反应越灵敏，值越小过渡越柔和 |
| `WindSpinScale` | 0.5 | 粒子受风时的旋转速度缩放。1.0 = 不变；小于 1 减弱旋转；大于 1 加强旋转 |

### 项目设置（编辑 → 项目设置 → 插件 → Leaf Field）

改动后需重启 PIE 生效。

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `VelocityFieldWidth` | 1000 cm | 速度场覆盖的范围（以玩家为中心的正方形区域），超出范围的角色不会产生影响 |
| `VelocityFieldRTSize` | 256 px | 速度场精度，通常 256 够用 |
| `WindMaxSpeed` | 1000 cm/s | 速度场能表达的最大速度上限。通常保持默认值，若角色移速远超 1000 cm/s 可适当调大 |
