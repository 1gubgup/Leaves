// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LeafInteractionSourceComponent.generated.h"

/**
 * 落叶交互速度场的"扰动源"组件。
 * 挂到任何会"扇起落叶"的 Actor 上（主角、敌人、可移动物体等）。
 *
 * 自身不直接写 RT，仅负责：
 *   1) 在 World 的 ULeafInteractionFieldSubsystem 中登记/注销自己
 *   2) 暴露半径 / 强度 / 速度计算给 Subsystem 读取
 *
 * 速度来源：默认通过 Owner 的位置差分得出（无须 Movement Component）。
 */
UCLASS(ClassGroup = (FX), meta = (BlueprintSpawnableComponent))
class LEAVES_API ULeafInteractionSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULeafInteractionSourceComponent();

	/** 笔刷半径（UV 空间，0~1）。0.1 ≈ 覆盖 CaptureWidth 的 10%。
	 *  CaptureWidth=500cm 时：0.2≈100cm、0.35≈175cm、0.5≈250cm 半径。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField", meta = (ClampMin = "0.01", ClampMax = "0.8"))
	float BrushRadiusUV = 0.35f;

	/** 速度强度倍率，1.0 表示原速度直接写入 RT；调大可让落叶更"炸"。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField", meta = (ClampMin = "0.0"))
	float VelocityStrength = 1.0f;

	/** 速度衰减时间常数（秒）。主角停下后，上报速度按 exp(-dt/Tau) 衰减。
	 *  0 = 不衰减（瞬时，老行为）；0.2~0.3 = 短促拖尾；0.6~1.0 = 明显风过留香。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField", meta = (ClampMin = "0.0"))
	float VelocityDecayTime = 0.25f;

	/** 是否使用"峰值保持"上报：本帧瞬时速度比衰减后的缓存大就立刻刷新。
	 *  true：起步零延迟、停步柔和衰减（推荐）；false：纯低通（起步也会有一点延迟）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField")
	bool bUsePeakHold = true;

	/** 取得当前帧速度（cm/s，世界空间 XY）。基于位置差分 + 衰减。 */
	FVector2D GetVelocityXY() const { return CachedVelocityXY; }

	/** 取得 Owner 的世界位置。 */
	FVector GetSourceWorldLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector PrevLocation = FVector::ZeroVector;
	FVector2D CachedVelocityXY = FVector2D::ZeroVector;
	bool bHasPrevLocation = false;
};
