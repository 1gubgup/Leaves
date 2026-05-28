// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LeafInteractionFieldSubsystem.generated.h"

class ULeafInteractionSourceComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;

/**
 * 落叶交互速度场 World Subsystem。
 *
 * 每帧管线：Clear → Splat → Push
 *   - Clear  : RT 清成 (0.5, 0.5, 0, 1)（编码后的零速度）
 *   - Splat  : 每个 Source 在自己位置画一个高斯笔刷 quad 到 RT
 *   - Push   : RT + 参数推到带 Tag = LeafFieldActorTag 的 NiagaraComponent
 *
 * 编码：写 = NormVel*0.5+0.5（[0,1]）；Niagara 解码 = (Sample.rg-0.5)*2。
 * CaptureCenter 跟随本地 Pawn。
 */
UCLASS()
class LEAVES_API ULeafInteractionFieldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }
	//~ End FTickableGameObject

	/** Source 组件注册 / 注销 */
	void RegisterSource(ULeafInteractionSourceComponent* Source);
	void UnregisterSource(ULeafInteractionSourceComponent* Source);

	// ============================================================
	// 通用配置
	// ============================================================

	/** RT 边长（仅运行时创建路径下生效；用持久化资产时以资产为准）。手机端 256 足够。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|General")
	int32 RTSize = 256;

	/** 风场覆盖的世界尺寸（cm），默认 500 = 5m。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|General")
	float CaptureWidth = 500.f;

	/** 用于查找接收风场的 NiagaraActor 的 Tag。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|General")
	FName LeafFieldActorTag = TEXT("LeafField");

	// ============================================================
	// 调感觉的 3 个旋钮（每帧推到 Niagara）
	// ============================================================

	/** 整体风强度倍率。想要更猛 → 1.5~2.5；想要更弱 → 0.5。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|Wind", meta = (ClampMin = "0.0"))
	float WindStrength = 1.0f;

	/** 水平风速 → 向上抬升的比例。想飘更高 → 0.5~0.8。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|Wind", meta = (ClampMin = "0.0"))
	float VerticalLift = 0.3f;

	/** 风对粒子的最大速度上限（cm/s）。想飞更快 → 1200~1600。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|Wind", meta = (ClampMin = "0.0"))
	float MaxWindSpeed = 800.f;

	// ============================================================
	// 固定参数（一般别动）
	// ============================================================

	/** 速度写入前除以这个值做归一化（cm/s）。与 MaxWindSpeed 配对，调一个就调另一个。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|Advanced", meta = (ClampMin = "1.0"))
	float VelocityScale = 600.f;

	/** PushToNiagara 日志间隔（帧），0 = 关闭。上线必须为 0。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|Advanced", meta = (ClampMin = "0"))
	int32 PushLogIntervalFrames = 0;

	/** 速度场 RT 资产路径。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|Advanced")
	FString VelocityRTAssetPath = TEXT("/Game/LeafField/RT_VelocityField_A.RT_VelocityField_A");

	/** Splat 材质路径（Surface Domain + AlphaComposite + Unlit）。 */
	UPROPERTY(EditAnywhere, Category = "LeafField|Advanced")
	FString SplatMaterialPath = TEXT("/Game/LeafField/M_FluidSplat.M_FluidSplat");

private:
	// ---------- Helpers ----------
	FVector2D WorldToUV(const FVector& WorldPos) const;
	void SplatPass();
	void PushToNiagara();

	/** 扫场景把所有带 Tag Actor 上的 NiagaraComponent 缓存起来。 */
	void RebuildNiagaraCache();
	/** 新 Actor 生成回调：若带目标 Tag，自动加入缓存。 */
	void OnActorSpawned(AActor* InActor);

	// ---------- Runtime ----------
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> RT = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SplatMaterial = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SplatMID = nullptr;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ULeafInteractionSourceComponent>> Sources;

	/** 缓存的目标 NiagaraComponent（避免每帧 TActorIterator 扫场） */
	TArray<TWeakObjectPtr<UNiagaraComponent>> CachedNiagaraComponents;

	FDelegateHandle ActorSpawnedHandle;
	FVector CaptureCenter = FVector::ZeroVector;
};
