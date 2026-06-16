#include "LeafFieldSubsystem.h"
#include "LeafFieldSettings.h"
#include "LeafInteractionSourceComponent.h"

#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeafField, Log, All);

namespace LeafFieldSplatParam
{
	static const FName P_CenterUV(TEXT("SplatCenterUV"));
	static const FName P_RadiusUV(TEXT("SplatRadiusUV"));
	static const FName P_Velocity(TEXT("SplatVelocity"));
	static const FLinearColor ZeroVelocityColor(0.5f, 0.5f, 0.f, 1.f);
}

const FString ULeafFieldSubsystem::VelocityRTAssetPath  = TEXT("/LeafField/LeafField/RT_VelocityField.RT_VelocityField");
const FString ULeafFieldSubsystem::SplatMaterialPath    = TEXT("/LeafField/LeafField/M_FluidSplat.M_FluidSplat");
const FString ULeafFieldSubsystem::NPCAssetPath         = TEXT("/LeafField/LeafField/NPC_LeafField.NPC_LeafField");

// ─── 生命周期 ──────────────────────────────────────────────────────────────────

void ULeafFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;

	const ULeafFieldSettings* S = GetDefault<ULeafFieldSettings>();
	if (S)
	{
		VelocityFieldWidth = S->VelocityFieldWidth;
		WindMaxSpeed       = S->WindMaxSpeed;
	}

	VelocityRT = LoadObject<UTextureRenderTarget2D>(nullptr, *VelocityRTAssetPath);
	if (!VelocityRT)
	{
		const int32 RTSize = S ? S->VelocityFieldRTSize : 128;
		VelocityRT = UKismetRenderingLibrary::CreateRenderTarget2D(
			this, RTSize, RTSize, RTF_RG8, LeafFieldSplatParam::ZeroVelocityColor, false);
	}

	if (VelocityRT)
	{
		VelocityRT->AddressX   = TA_Clamp;
		VelocityRT->AddressY   = TA_Clamp;
		VelocityRT->ClearColor = LeafFieldSplatParam::ZeroVelocityColor;
		VelocityRT->UpdateResourceImmediate(true);
	}

	SplatMaterial = LoadObject<UMaterialInterface>(nullptr, *SplatMaterialPath);
	if (SplatMaterial)
	{
		SplatMID = UMaterialInstanceDynamic::Create(SplatMaterial, this);
	}
}

void ULeafFieldSubsystem::Deinitialize()
{
	Sources.Reset();
	SplatMaterial = nullptr;
	SplatMID      = nullptr;
	VelocityRT    = nullptr;
	NPCInstance   = nullptr;
	bNPCResolved  = false;

	Super::Deinitialize();
}

TStatId ULeafFieldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULeafFieldSubsystem, STATGROUP_Tickables);
}

// ─── 注册接口 ──────────────────────────────────────────────────────────────────

void ULeafFieldSubsystem::RegisterSource(ULeafInteractionSourceComponent* Source)
{
	if (Source) Sources.AddUnique(Source);
}

void ULeafFieldSubsystem::UnregisterSource(ULeafInteractionSourceComponent* Source)
{
	if (!Source) return;
	Sources.RemoveAll([Source](const TWeakObjectPtr<ULeafInteractionSourceComponent>& W)
	{
		return !W.IsValid() || W.Get() == Source;
	});
}

// ─── Tick ──────────────────────────────────────────────────────────────────────

void ULeafFieldSubsystem::EnsureNPCInstance()
{
	if (bNPCResolved) return;
	bNPCResolved = true;

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;

	UNiagaraParameterCollection* NPC = LoadObject<UNiagaraParameterCollection>(nullptr, *NPCAssetPath);
	if (!NPC) return;

	NPCInstance = UNiagaraFunctionLibrary::GetNiagaraParameterCollection(World, NPC);
	if (NPCInstance)
	{
		NPCInstance->SetFloatParameter(TEXT("VelocityFieldWidth"), VelocityFieldWidth);
		NPCInstance->SetFloatParameter(TEXT("WindMaxSpeed"),       WindMaxSpeed);
	}
}

void ULeafFieldSubsystem::Tick(float DeltaTime)
{
	if (!VelocityRT) return;

	UWorld* World = GetWorld();
	if (!World) return;

	EnsureNPCInstance();

	// 速度场中心每帧跟随玩家
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			VelocityFieldCenter = Pawn->GetActorLocation();
		}
	}

	// Clear + Splat
	if (SplatMID)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, VelocityRT, LeafFieldSplatParam::ZeroVelocityColor);
		SplatPass();
	}

	// 写入 VelocityFieldCenter 供 Niagara 模块读取
	if (NPCInstance)
	{
		NPCInstance->SetVectorParameter(TEXT("VelocityFieldCenter"), VelocityFieldCenter);
	}
}

// ─── Splat ─────────────────────────────────────────────────────────────────────

FVector2D ULeafFieldSubsystem::WorldToVelocityUV(const FVector& WorldPos) const
{
	const float HalfW = VelocityFieldWidth * 0.5f;
	return FVector2D(
		(WorldPos.X - (VelocityFieldCenter.X - HalfW)) / VelocityFieldWidth,
		(WorldPos.Y - (VelocityFieldCenter.Y - HalfW)) / VelocityFieldWidth);
}

void ULeafFieldSubsystem::SplatPass()
{
	if (!SplatMID || !VelocityRT || Sources.Num() == 0) return;

	const float InvVelScale = (WindMaxSpeed > KINDA_SMALL_NUMBER) ? (1.f / WindMaxSpeed) : 1.f;

	ULeafInteractionSourceComponent* BestSrc = nullptr;
	float BestMag = 0.f;

	for (auto It = Sources.CreateIterator(); It; ++It)
	{
		ULeafInteractionSourceComponent* Src = It->Get();
		if (!Src) { It.RemoveCurrent(); continue; }

		const FVector2D UV = WorldToVelocityUV(Src->GetSourceWorldLocation());
		if (UV.X < 0.f || UV.X > 1.f || UV.Y < 0.f || UV.Y > 1.f) continue;

		const float Mag = Src->GetVelocityXY().Size();
		if (Mag > BestMag) { BestMag = Mag; BestSrc = Src; }
	}

	if (!BestSrc || BestMag < KINDA_SMALL_NUMBER) return;

	const FVector2D SplatUV = WorldToVelocityUV(BestSrc->GetSourceWorldLocation());
	const FVector2D NormVel = BestSrc->GetVelocityXY() * InvVelScale;
	const float RadiusUV    = (VelocityFieldWidth > KINDA_SMALL_NUMBER)
		? (BestSrc->BrushRadiusWorld / VelocityFieldWidth) : 0.f;

	SplatMID->SetVectorParameterValue(LeafFieldSplatParam::P_CenterUV, FLinearColor(SplatUV.X, SplatUV.Y, 0.f, 0.f));
	SplatMID->SetScalarParameterValue(LeafFieldSplatParam::P_RadiusUV, RadiusUV);
	SplatMID->SetVectorParameterValue(LeafFieldSplatParam::P_Velocity,  FLinearColor(NormVel.X, NormVel.Y, 0.f, 1.f));

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, VelocityRT, SplatMID);
}
