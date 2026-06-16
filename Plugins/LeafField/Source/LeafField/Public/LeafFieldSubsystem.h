// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LeafFieldSubsystem.generated.h"

class ULeafInteractionSourceComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraParameterCollectionInstance;

/**
 * LeafField 全局速度场维护器（WorldSubsystem，自动随 GameWorld 创建/销毁）。
 *
 * 职责：
 *   1. 每帧 Clear + Splat 全局速度场 RT（VelocityRT）
 *   2. 每帧把 VelocityFieldCenter 写入 NPC_LeafField，供 WindInteraction Niagara 模块读取
 *
 * 使用方式：
 *   - 角色身上挂 ULeafInteractionSourceComponent，BeginPlay 时自动注册到本 Subsystem
 *   - 任何 Niagara System 挂 WindInteraction 模块并引用 NPC_LeafField，无需额外对接
 *
 * 全局参数（VelocityFieldWidth / VelocityFieldRTSize / WindMaxSpeed）
 * 位于 项目设置 → 插件 → Leaf Field。
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
	virtual void         Tick(float DeltaTime) override;
	virtual TStatId      GetStatId() const override;
	virtual bool         IsTickable() const override           { return true; }
	virtual bool         IsTickableInEditor() const override   { return false; }
	virtual bool         IsTickableWhenPaused() const override { return false; }

	// ── 注册接口（由 ULeafInteractionSourceComponent 调用）────────
	void RegisterSource(ULeafInteractionSourceComponent* Source);
	void UnregisterSource(ULeafInteractionSourceComponent* Source);

private:
	// ── 资产路径常量 ──────────────────────────────────────────────
	static const FString VelocityRTAssetPath;
	static const FString SplatMaterialPath;
	static const FString NPCAssetPath;

	// ── 内部方法 ──────────────────────────────────────────────────
	/** 首帧延迟获取 NPC 实例（Initialize 时 NiagaraWorldManager 尚未就绪） */
	void      EnsureNPCInstance();
	FVector2D WorldToVelocityUV(const FVector& WorldPos) const;
	void      SplatPass();

	// ── 速度场资源 ────────────────────────────────────────────────
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D>           VelocityRT    = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface>               SplatMaterial = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic>         SplatMID      = nullptr;
	UPROPERTY(Transient) TObjectPtr<UNiagaraParameterCollectionInstance> NPCInstance = nullptr;

	// ── 扰动源注册表 ──────────────────────────────────────────────
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ULeafInteractionSourceComponent>> Sources;

	/** 速度场中心（XY 跟随本地 Pawn，每帧更新） */
	FVector VelocityFieldCenter = FVector::ZeroVector;

	// 从项目设置缓存（Initialize 读取一次）
	float VelocityFieldWidth = 1000.f;
	float WindMaxSpeed       = 1000.f;

	/** NPC 实例是否已尝试解析（保证只在首帧获取一次） */
	bool bNPCResolved = false;
};
