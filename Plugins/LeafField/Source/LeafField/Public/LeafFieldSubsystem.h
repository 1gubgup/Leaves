// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LeafFieldSubsystem.generated.h"

class ULeafInteractionSourceComponent;
class ALeafInteractionField;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * LeafField 注册中心 + 全局速度场。
 *
 * 数据流：
 *   Source(角色) ──注册──▶ Subsystem ──每帧 Splat──▶ 全局 VelocityRT
 *                                       │
 *                                       └──每帧 Push──▶ ActiveFields[*].PushDynamicParams()
 *
 *   Field(区域) ──注册/激活──▶ Subsystem.ActiveFields
 *
 * Subsystem 不持有 Field 自己的状态（高度图、Niagara 参数等都在 Field 上），
 * 只负责：维护两份注册表 / 全局速度场的 Clear+Splat / 把全局速度场参数派发给所有激活的 Field。
 *
 * 全局配置（VelocityFieldWidth / VelocityFieldRTSize / WindMaxSpeed）
 * 已迁移至 ULeafFieldSettings（项目设置 → 插件 → Leaf Field）。
 * WindStrength / WindLift 已迁移至 ALeafInteractionField（每个 Field 独立设置）。
 */
UCLASS()
class LEAFFIELD_API ULeafFieldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ============================================================
	// 注册接口
	// ============================================================

	/** 角色身上的 Source 组件在 BeginPlay 时调用 */
	void RegisterSource(ULeafInteractionSourceComponent* Source);
	void UnregisterSource(ULeafInteractionSourceComponent* Source);

	/** Field 进入/离开 Active 状态时调用，Subsystem 据此决定是否每帧推参数给它 */
	void NotifyFieldActivated(ALeafInteractionField* Field);
	void NotifyFieldDeactivated(ALeafInteractionField* Field);

	// ============================================================
	// 给 Field 取用的访问器
	// ============================================================

	UTextureRenderTarget2D* GetVelocityRT() const { return VelocityRT; }
	const FVector& GetVelocityFieldCenter() const { return VelocityFieldCenter; }

	/** 速度场覆盖范围（从 ULeafFieldSettings 读取，供 Field 查询） */
	float GetVelocityFieldWidth() const { return VelocityFieldWidth; }

	/** 风速编码基准（从 ULeafFieldSettings 读取，供 Field 推给 Niagara） */
	float GetWindMaxSpeed() const { return WindMaxSpeed; }

private:
	// ============================================================
	// 内部实现
	// ============================================================

	static const FString VelocityRTAssetPath;
	static const FString SplatMaterialPath;

	FVector2D WorldToVelocityUV(const FVector& WorldPos) const;
	void SplatPass();

	// 速度场资源
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D>   VelocityRT = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface>       SplatMaterial = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SplatMID = nullptr;

	// 注册表
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ULeafInteractionSourceComponent>> Sources;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ALeafInteractionField>> ActiveFields;

	/** 速度场跟随的中心（XY 跟本地 Pawn） */
	FVector VelocityFieldCenter = FVector::ZeroVector;

	// 从 ULeafFieldSettings 缓存的值（Initialize 时读取一次）
	float VelocityFieldWidth = 500.f;
	float WindMaxSpeed       = 500.f;
};
