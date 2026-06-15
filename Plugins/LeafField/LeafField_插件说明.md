# LeafField 落叶交互插件

## 一、功能概述

提供一套**速度场驱动机制**，供任意 Niagara System 通过 `WindInteraction` 模块接入：

- 角色经过时，速度被写入全局速度场 RT；
- Niagara System 挂载 `WindInteraction` 模块后，粒子自动采样速度场获得风速并被吹动。

**数据流：**
```
角色移动 → ULeafInteractionSourceComponent 计算速度
        → ULeafFieldSubsystem 每帧 Splat → VelocityRT
        → NPC_LeafField.VelocityFieldCenter / VelocityRT 每帧更新
        → WindInteraction 模块从 NPC 读取参数 → 粒子受力
```

---

## 二、技术方案

| 技术点 | 用途 |
|--------|------|
| **RenderTarget（速度场，RG8）** | 全局一张，记录角色移动产生的风速 |
| **DrawMaterialToRenderTarget** | 使用 Splat 材质将角色速度绘制进速度场 RT |
| **Niagara Parameter Collection** | 全局参数共享，WindInteraction 模块直接读取，无需 User 参数中转 |
| **WorldSubsystem** | 每帧维护速度场并向 NPC 写入参数 |
| **DeveloperSettings** | 项目设置中的全局配置项 |

---

## 三、模块构成

### C++ 类

| 类 | 职责 |
|----|------|
| `ULeafFieldSettings` | 全局项目设置：速度场尺寸、RT 分辨率、风速编码基准 |
| `ULeafFieldSubsystem` | 全局枢纽：每帧清空并绘制速度场，向 NPC_LeafField 写入参数 |
| `ULeafInteractionSourceComponent` | 扰动源组件：挂在角色上，通过位置差分计算每帧速度 |

### 内容资产（路径硬编码，不可移动或重命名）

```
Plugins/LeafField/Content/LeafField/
├── RT_VelocityField    速度场渲染目标，RG8 格式，清屏色必须为 (0.5, 0.5, 0, 1)
├── M_FluidSplat        Splat 材质，参数：SplatCenterUV / SplatRadiusUV / SplatVelocity
├── NPC_LeafField       Niagara Parameter Collection，向所有挂了 WindInteraction 的 System 广播参数
└── WindInteraction     Niagara Module Script，从 NPC_LeafField 读取参数，驱动粒子受力
```

### NPC_LeafField 参数表

| 参数名 | 类型 | 说明 |
|--------|------|------|
| `VelocityRT` | Texture Object | 速度场 RT，启动时写入一次 |
| `VelocityFieldCenter` | Vector | 速度场中心世界坐标，每帧更新 |
| `VelocityFieldWidth` | Float | 速度场覆盖边长（cm），启动时写入一次 |
| `WindMaxSpeed` | Float | 速度场 RG8 编码基准（cm/s），启动时写入一次 |

---

## 四、接入方式

### WindInteraction 模块（随插随用）

1. 在自己的 Niagara System 的 **Particle Update** 阶段添加 `WindInteraction` 模块；
2. 确认模块输入引脚绑定到 `NPC_LeafField` 对应参数（非 User 参数）；
3. **完成**。无需任何 C++ 对接代码，参数由 `ULeafFieldSubsystem` 自动维护。

### 扰动源（角色接入）

在角色 Blueprint 上添加 `LeafInteractionSourceComponent` 组件，调整以下参数：

| 参数 | 默认 | 说明 |
|------|------|------|
| `BrushRadiusWorld` | 200 cm | 速度 Splat 笔刷半径 |
| `VelocityStrength` | 1.0 | 速度倍率 |
| `VelocityDecayTime` | 0.1 s | 停止移动后速度衰减时长 |
| `bUsePeakHold` | true | 峰值保持：起步零延迟，停步柔和衰减 |

### 引擎模块依赖

```
Niagara / NiagaraCore / RenderCore / DeveloperSettings / Engine / CoreUObject
```

---

## 五、全局配置（项目设置 → 插件 → Leaf Field）

| 参数 | 默认 | 说明 |
|------|------|------|
| `VelocityFieldWidth` | 1000 cm | 速度场覆盖边长，以本地玩家为中心，改后需重启 PIE |
| `VelocityFieldRTSize` | 256 | 速度场 RT 分辨率（px），改后需重启 PIE |
| `WindMaxSpeed` | 1000 cm/s | RG8 编码基准；编解码两端必须一致，**勿随意修改** |

---

## 六、WindInteraction 模块参数说明

### 来自 NPC（自动注入，无需手动绑定）

| 参数 | 说明 |
|------|------|
| `VelocityRT` | 速度场 RT |
| `VelocityFieldCenter` | 速度场中心（每帧跟随玩家） |
| `VelocityFieldWidth` | 速度场覆盖范围 |
| `WindMaxSpeed` | 编解码基准 |

### 模块内部可调（直接在 Niagara 编辑器中调整）

| 参数 | 默认 | 说明 |
|------|------|------|
| `WindStrength` | 1.0 | 风强倍率（0=无风，2=双倍） |
| `WindLift` | 0.05 | 水平风速转垂直上抬力比例 |
| `WindResponseSpeed` | 5.0 | 粒子跟随风场的响应速度（帧率无关） |
| `WindSpinImpulse` | 1.0 | 被风吹起时的翻滚冲量强度 |

---

## 七、目录结构

```
Plugins/LeafField/
├── LeafField.uplugin
├── LeafField_插件说明.md
└── Source/LeafField/
    ├── Public/
    │   ├── LeafFieldSettings.h               全局项目设置
    │   ├── LeafFieldSubsystem.h              世界子系统（速度场 + NPC 写入）
    │   └── LeafInteractionSourceComponent.h  扰动源组件（挂在角色上）
    └── Private/
        ├── LeafFieldModule.cpp
        ├── LeafFieldSettings.cpp
        ├── LeafFieldSubsystem.cpp
        └── LeafInteractionSourceComponent.cpp
```
