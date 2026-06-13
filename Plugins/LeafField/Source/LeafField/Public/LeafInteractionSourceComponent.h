// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LeafInteractionSourceComponent.generated.h"

/**
 * 落叶交互的"扰动源"组件，挂在角色（或任何会扇起叶子的 Actor）身上。
 *
 * 职责：
 *   - 在 ULeafFieldSubsystem 中登记/注销自己
 *   - 通过 Owner 位置差分得出当前帧速度，供 Subsystem 写入全局速度场
 *
 * 不直接读写 RT，不依赖 Movement Component。
 */
UCLASS(ClassGroup = (FX), meta = (BlueprintSpawnableComponent))
class LEAFFIELD_API ULeafInteractionSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULeafInteractionSourceComponent();

	/** Splat 笔刷半径（UV 空间，0~1）。在 VelocityFieldWidth=500cm 时：0.2≈100cm、0.35≈175cm 半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField",
		meta = (ClampMin = "0.01", ClampMax = "0.8"))
	float BrushRadiusUV = 0.35f;

	/** 速度倍率，调大让叶子被扇得更猛 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField",
		meta = (ClampMin = "0.0"))
	float VelocityStrength = 1.0f;

	/** 速度衰减时间常数（秒）。0 = 瞬时无衰减；0.2~0.3 = 短促拖尾；0.6~1.0 = 风过留香 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField",
		meta = (ClampMin = "0.0"))
	float VelocityDecayTime = 0.25f;

	/** 峰值保持：起步零延迟、停步柔和衰减（推荐 true）；false = 纯低通（起步也有延迟） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField")
	bool bUsePeakHold = true;

	/** 当前帧速度（cm/s，世界空间 XY） */
	FVector2D GetVelocityXY() const { return CachedVelocityXY; }

	/** Owner 世界位置 */
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
