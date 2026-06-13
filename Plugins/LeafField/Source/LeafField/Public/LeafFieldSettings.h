// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LeafFieldSettings.generated.h"

/**
 * LeafField 全局项目设置。
 * 出现在：编辑器 → 项目设置 → 插件 → Leaf Field
 *
 * 这里只放「必须全局一致」或「所有 Field 共享同一份 RT 资源」的参数：
 *
 *   VelocityFieldWidth   — 速度场覆盖尺寸（cm）
 *   VelocityFieldRTSize  — 速度场 RT 分辨率（px），改后需重启 PIE
 *   WindMaxSpeed         — 速度场编码基准（cm/s），编解码必须全局匹配
 *
 * 每个 Field 独立的参数（WindStrength / WindLift 等）请在 ALeafInteractionField 上设置。
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Leaf Field"))
class LEAFFIELD_API ULeafFieldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 将本插件的设置归入编辑器「插件」分类下 */
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// ============================================================
	// 高度图（HeightField）
	// ============================================================

	/**
	 * 高度图 RT 分辨率（像素，正方形）。
	 * 决定贴地法线的采样精度：越大贴地越精细，但每个 Field 多一张 RT 内存。
	 * 典型值：128（低精度省内存）/ 256（默认）/ 512（高精度大地形）。
	 * 改动后需重启 PIE 生效（RT 在 BeginPlay 时创建）。
	 */
	UPROPERTY(EditAnywhere, config, Category = "HeightField",
		meta = (ClampMin = "64", ClampMax = "512",
		        ToolTip = "高度图 RT 分辨率（像素）。改动后需重启 PIE 生效。"))
	int32 HeightRTSize = 256;

	// ============================================================
	// 速度场（VelocityField）
	// ============================================================

	/**
	 * 速度场覆盖范围（cm，正方形边长）。
	 * 角色在此范围内行走才会扰动叶子；超出范围则速度写不进 RT。
	 * 典型值：500（5m）~ 1000（10m）。
	 */
	UPROPERTY(EditAnywhere, config, Category = "VelocityField",
		meta = (ClampMin = "100.0", ClampMax = "2000.0", ForceUnits = "cm",
		        ToolTip = "速度场覆盖范围（cm，正方形边长）。改动后需重启 PIE 生效（Subsystem 初始化时读取一次）。"))
	float VelocityFieldWidth = 500.f;

	/**
	 * 速度场 RT 分辨率（像素，正方形）。
	 * 更高分辨率 = 更精细的速度纹理，但 GPU 内存和带宽开销增大。
	 * 典型值：64 / 128 / 256。改动后需重启 PIE 生效。
	 */
	UPROPERTY(EditAnywhere, config, Category = "VelocityField",
		meta = (ClampMin = "64", ClampMax = "512",
		        ToolTip = "速度场 RT 分辨率（像素）。改动后需重启 PIE 生效。"))
	int32 VelocityFieldRTSize = 128;

	// ============================================================
	// 风力编码基准（Wind）
	// ============================================================

	/**
	 * 风速编码基准 / 速度上限（cm/s）。
	 *
	 * VelocityRT 以 RG8 格式编码：NormVel = rawVel / WindMaxSpeed，
	 * Niagara 解码时乘回来。编码端（Splat）和解码端（Niagara）必须用同一个值，
	 * 因此这个参数必须全局一致。
	 *
	 * 典型值：1000（默认）。调大可支持更高风速但降低低速精度。
	 */
	UPROPERTY(EditAnywhere, config, Category = "Wind",
		meta = (ClampMin = "100.0", ClampMax = "5000.0", ForceUnits = "cm/s",
		        ToolTip = "速度场 RG8 编码基准（cm/s）。编解码必须全局匹配，轻易不要修改。"))
	float WindMaxSpeed = 1000.f;
};
