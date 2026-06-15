// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LeafFieldSettings.generated.h"

/**
 * LeafField 全局项目设置。
 * 出现在：编辑器 → 项目设置 → 插件 → Leaf Field
 *
 * 这里只放「必须全局一致」的速度场参数：
 *
 *   VelocityFieldWidth   — 速度场覆盖尺寸（cm），同时写入 NPC_LeafField
 *   VelocityFieldRTSize  — 速度场 RT 分辨率（px），改后需重启 PIE
 *   WindMaxSpeed         — 速度场编码基准（cm/s），编解码必须全局匹配，同时写入 NPC_LeafField
 *
 * WindInteraction 模块内部可调参数（WindStrength / WindLift 等）直接在 Niagara 编辑器中调整。
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Leaf Field"))
class LEAFFIELD_API ULeafFieldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 将本插件的设置归入编辑器「插件」分类下 */
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// ============================================================
	// 速度场（VelocityField）
	// ============================================================

	/**
	 * 速度场覆盖范围（cm，正方形边长）。
	 * 角色在此范围内行走才会扰动粒子；超出范围则速度写不进 RT。
	 * 典型值：500（5m）~ 1000（10m）。
	 * 改动后需重启 PIE 生效（Subsystem 初始化时读取一次并写入 NPC_LeafField）。
	 */
	UPROPERTY(EditAnywhere, config, Category = "VelocityField",
		meta = (ClampMin = "100.0", ClampMax = "2000.0", ForceUnits = "cm",
		        ToolTip = "速度场覆盖范围（cm，正方形边长）。改动后需重启 PIE 生效。"))
	float VelocityFieldWidth = 1000.f;

	/**
	 * 速度场 RT 分辨率（像素，正方形）。
	 * 更高分辨率 = 更精细的速度纹理，但 GPU 内存和带宽开销增大。
	 * 典型值：64 / 128 / 256。改动后需重启 PIE 生效。
	 */
	UPROPERTY(EditAnywhere, config, Category = "VelocityField",
		meta = (ClampMin = "64", ClampMax = "512",
		        ToolTip = "速度场 RT 分辨率（像素）。改动后需重启 PIE 生效。"))
	int32 VelocityFieldRTSize = 256;

	// ============================================================
	// 风力编码基准（Wind）
	// ============================================================

	/**
	 * 风速编码基准 / 速度上限（cm/s）。
	 *
	 * VelocityRT 以 RG8 格式编码：NormVel = rawVel / WindMaxSpeed，
	 * WindInteraction 模块解码时乘回来。编码端（Splat）和解码端（模块）必须用同一个值，
	 * 因此这个参数必须全局一致，同时写入 NPC_LeafField。
	 *
	 * 典型值：1000（默认）。调大可支持更高风速但降低低速精度。
	 */
	UPROPERTY(EditAnywhere, config, Category = "Wind",
		meta = (ClampMin = "100.0", ClampMax = "5000.0", ForceUnits = "cm/s",
		        ToolTip = "速度场 RG8 编码基准（cm/s）。编解码必须全局匹配，轻易不要修改。"))
	float WindMaxSpeed = 1000.f;
};
