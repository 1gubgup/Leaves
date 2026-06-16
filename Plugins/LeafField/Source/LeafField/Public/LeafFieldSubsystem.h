#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LeafFieldSubsystem.generated.h"

class ULeafInteractionSourceComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraParameterCollectionInstance;

UCLASS()
class LEAFFIELD_API ULeafFieldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void    Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void    Deinitialize() override;
	virtual void    Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool    IsTickable() const override          { return true; }
	virtual bool    IsTickableInEditor() const override  { return false; }
	virtual bool    IsTickableWhenPaused() const override{ return false; }

	void RegisterSource(ULeafInteractionSourceComponent* Source);
	void UnregisterSource(ULeafInteractionSourceComponent* Source);

private:
	static const FString VelocityRTAssetPath;
	static const FString SplatMaterialPath;
	static const FString NPCAssetPath;

	void      EnsureNPCInstance();
	FVector2D WorldToVelocityUV(const FVector& WorldPos) const;
	void      SplatPass();

	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D>              VelocityRT    = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface>                  SplatMaterial = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic>            SplatMID      = nullptr;
	UPROPERTY(Transient) TObjectPtr<UNiagaraParameterCollectionInstance> NPCInstance   = nullptr;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ULeafInteractionSourceComponent>> Sources;

	FVector VelocityFieldCenter = FVector::ZeroVector;
	float   VelocityFieldWidth  = 1000.f;
	float   WindMaxSpeed        = 1000.f;
	bool    bNPCResolved        = false;
};
