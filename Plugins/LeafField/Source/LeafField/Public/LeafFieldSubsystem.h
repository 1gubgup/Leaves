// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LeafFieldSubsystem.generated.h"

class ULeafInteractionSourceComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;

/**
 * LeafField 全局速度场维护器。
 *
 * 职责：
 *   1. 每帧 Clear + Splat 全局速度场 RT（VelocityRT）
 *   2. 每帧把 VelocityFieldCenter / VelocityRT / VelocityFieldWidth / WindMaxSpeed
 *      写入 NPC_LeafField，供 WindInteraction Niagara 模块直接读取。
 *
 * WindInteraction 模块随插随用：
 *   任何 Niagara System 挂入 WindInteraction 模块并引用 NPC_LeafField 后，
 *   无需额外 C++ 对接，参数自动流入。
 *
 * 全局配置（VelocityFieldWidth / VelocityFieldRTSize / WindMaxSpeed）
 * 位于 ULeafFieldSettings（项目设置 → 插件 → Leaf Field）。
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
	// 注册接口（角色身上的 Source 组件使用）
	// ============================================================

	/** 角色身上的 Source 组件在 BeginPlay 时调用 */
	void RegisterSource(ULeafInteractionSourceComponent* Source);
	void UnregisterSource(ULeafInteractionSourceComponent* Source);

private:
	// ============================================================
	// 内部实现
	// ============================================================

	static const FString VelocityRTAssetPath;
	static const FString SplatMaterialPath;
	static const FString NPCAssetPath;

	FVector2D WorldToVelocityUV(const FVector& WorldPos) const;
	void SplatPass();

	// 速度场资源
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D>   VelocityRT = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface>       SplatMaterial = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SplatMID = nullptr;

	// NPC：供 WindInteraction 模块随插随用，与具体 Niagara System 无关
	UPROPERTY(Transient) TObjectPtr<UNiagaraParameterCollectionInstance> NPCInstance = nullptr;

	// 注册表（扰动源）
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ULeafInteractionSourceComponent>> Sources;

	/** 速度场跟随的中心（XY 跟本地 Pawn） */
	FVector VelocityFieldCenter = FVector::ZeroVector;

	// 从 ULeafFieldSettings 缓存的值（Initialize 时读取一次）
	float VelocityFieldWidth = 500.f;
	float WindMaxSpeed       = 500.f;
};
