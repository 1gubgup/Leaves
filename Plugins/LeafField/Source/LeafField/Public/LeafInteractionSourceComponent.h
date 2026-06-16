#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LeafInteractionSourceComponent.generated.h"

// 挂在角色身上，自动向 LeafFieldSubsystem 注册，提供每帧移动速度用于写入速度场
UCLASS(ClassGroup = (FX), meta = (BlueprintSpawnableComponent))
class LEAFFIELD_API ULeafInteractionSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULeafInteractionSourceComponent();

	// 角色影响周围粒子的范围半径，调大则更远处的粒子也会被扰动
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField", meta = (ClampMin = "1.0", ClampMax = "5000.0", Units = "cm"))
	float BrushRadiusWorld = 200.f;

	// 角色移动对粒子的推力倍率，调大叶子被吹得更猛
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField", meta = (ClampMin = "0.0"))
	float VelocityStrength = 1.0f;

	// 角色停步后推力消散的时间（秒）。0 = 立刻消失；调大则停步后有余风效果
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField", meta = (ClampMin = "0.0"))
	float VelocityDecayTime = 0.1f;

	// 保持 true。起步时推力立即生效，停步时柔和消散
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField")
	bool bUsePeakHold = true;

	FVector2D GetVelocityXY() const { return CachedVelocityXY; }
	FVector   GetSourceWorldLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector   PrevLocation     = FVector::ZeroVector;
	FVector2D CachedVelocityXY = FVector2D::ZeroVector;
	bool      bHasPrevLocation = false;
};
