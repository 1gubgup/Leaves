# LeafField 插件 · 使用手册

本插件提供一套**速度场驱动机制**，供任意 Niagara System 通过 `WindInteraction` 模块接入，使粒子能感知角色移动产生的风速。

---

## 快速接入（两步）

### 第一步：给角色挂载扰动源组件

打开角色蓝图，在 Components 面板 `Add Component`，搜索并添加 `LeafInteractionSource`。

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `Brush Radius World` | 200 cm | 角色周围的扰动半径 |
| `Velocity Strength` | 1.0 | 扰动力度倍率，大型单位可调到 2~3 |
| `Velocity Decay Time` | 0.1 s | 停步后速度消失的快慢 |

> 不挂这个组件则没有任何扰动来源，速度场保持静止。

---

### 第二步：在 Niagara System 中使用 WindInteraction 模块

1. 打开你的 Niagara System，在 **Particle Update** 阶段点 `+` 添加模块；
2. 搜索并添加 `WindInteraction`；
3. 确认模块的输入引脚绑定到 `NPC_LeafField` 对应参数（不是 User 参数）；
4. **完成**，无需任何额外 C++ 或蓝图代码。

---

## WindInteraction 模块参数

### 自动注入（来自 NPC_LeafField，无需手动操作）

| 参数 | 说明 |
|------|------|
| `VelocityRT` | 速度场 RT，由插件维护 |
| `VelocityFieldCenter` | 速度场中心，每帧跟随本地玩家 |
| `VelocityFieldWidth` | 速度场覆盖范围 |
| `WindMaxSpeed` | RG8 编码基准 |

### 模块内部可调（直接在 Niagara 编辑器中调整）

| 参数 | 默认值 | 效果说明 |
|------|--------|----------|
| `WindStrength` | 1.0 | 整体风力强度（0=无风，2=双倍） |
| `WindLift` | 0.05 | 水平风转垂直上抬力比例（0~1） |
| `WindResponseSpeed` | 5.0 | 粒子跟随风场的响应速度，越大跟风越快 |
| `WindSpinImpulse` | 1.0 | 被风吹起时的翻滚冲量强度 |

---

## 全局配置（项目设置 → 插件 → Leaf Field）

**一般不需要修改**，如有特殊需求可调整：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `Velocity Field Width` | 1000 cm | 速度场覆盖范围，以本地玩家为中心，改后需重启 PIE |
| `Velocity Field RT Size` | 256 px | 速度场贴图精度，改后需重启 PIE |
| `Wind Max Speed` | 1000 cm/s | 速度编码基准，编解码必须匹配，**不要修改** |

---

## 故障排查

| 现象 | 解决方法 |
|------|----------|
| **粒子不被角色扰动** | 角色蓝图是否挂了 `LeafInteractionSource` 组件？角色是否在速度场范围内（默认 1000 cm，以玩家为中心） |
| **停步后粒子还朝一个方向飘** | 速度场 RT 清除颜色设置有误，检查 `RT_VelocityField` 的 ClearColor 是否为 `(0.5, 0.5, 0, 1)` |
| **WindInteraction 模块没有反应** | ① 确认 Emitter 的 `Sim Target` 是否为 `GPUComputeSim`；② 模块输入引脚是否绑定到 `NPC_LeafField`（而非 User 参数）；③ `NPC_LeafField` 资产是否存在于 `/LeafField/LeafField/` 路径 |
