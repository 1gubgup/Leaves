// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeafFieldSubsystem.h"
#include "LeafFieldSettings.h"
#include "LeafInteractionSourceComponent.h"
#include "LeafInteractionField.h"

#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeafField, Log, All);

// ============================================================
// 常量
// ============================================================
namespace LeafFieldSplatParam
{
	static const FName P_CenterUV (TEXT("SplatCenterUV"));
	static const FName P_RadiusUV (TEXT("SplatRadiusUV"));
	static const FName P_Velocity (TEXT("SplatVelocity"));

	// 速度场零速编码：(0.5, 0.5, 0, 1)，解码 NormVel = (rg - 0.5) * 2
	static const FLinearColor ZeroVelocityColor(0.5f, 0.5f, 0.f, 1.f);
}

const FString ULeafFieldSubsystem::VelocityRTAssetPath =
	TEXT("/LeafField/LeafField/RT_VelocityField.RT_VelocityField");
const FString ULeafFieldSubsystem::SplatMaterialPath =
	TEXT("/LeafField/LeafField/M_FluidSplat.M_FluidSplat");

// ============================================================
// 生命周期
// ============================================================

void ULeafFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ── 从项目设置读取全局参数 ──────────────────────────────────
	const ULeafFieldSettings* S = GetDefault<ULeafFieldSettings>();
	if (S)
	{
		VelocityFieldWidth = S->VelocityFieldWidth;
		WindMaxSpeed       = S->WindMaxSpeed;
	}

	// ── 速度场 RT：优先用磁盘资产，否则代码创建 ────────────────
	VelocityRT = LoadObject<UTextureRenderTarget2D>(nullptr, *VelocityRTAssetPath);
	if (!VelocityRT)
	{
		const int32 RTSize = S ? S->VelocityFieldRTSize : 128;
		VelocityRT = UKismetRenderingLibrary::CreateRenderTarget2D(
			this, RTSize, RTSize,
			RTF_RG8, LeafFieldSplatParam::ZeroVelocityColor, false);
	}

	if (VelocityRT)
	{
		VelocityRT->AddressX = TA_Clamp;
		VelocityRT->AddressY = TA_Clamp;
		VelocityRT->ClearColor = LeafFieldSplatParam::ZeroVelocityColor;
		VelocityRT->UpdateResourceImmediate(true);
	}

	// ── Splat 材质 ──────────────────────────────────────────────
	SplatMaterial = LoadObject<UMaterialInterface>(nullptr, *SplatMaterialPath);
	if (SplatMaterial)
	{
		SplatMID = UMaterialInstanceDynamic::Create(SplatMaterial, this);
	}
	else
	{
		UE_LOG(LogLeafField, Warning, TEXT("[LeafField] Splat material not found: %s"), *SplatMaterialPath);
	}
}

void ULeafFieldSubsystem::Deinitialize()
{
	Sources.Reset();
	ActiveFields.Reset();
	SplatMaterial = nullptr;
	SplatMID = nullptr;
	VelocityRT = nullptr;

	Super::Deinitialize();
}

TStatId ULeafFieldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULeafFieldSubsystem, STATGROUP_Tickables);
}

// ============================================================
// 注册
// ============================================================

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

void ULeafFieldSubsystem::NotifyFieldActivated(ALeafInteractionField* Field)
{
	if (Field) ActiveFields.AddUnique(Field);
}

void ULeafFieldSubsystem::NotifyFieldDeactivated(ALeafInteractionField* Field)
{
	if (!Field) return;
	ActiveFields.RemoveAll([Field](const TWeakObjectPtr<ALeafInteractionField>& W)
	{
		return !W.IsValid() || W.Get() == Field;
	});
}

// ============================================================
// Tick
// ============================================================

void ULeafFieldSubsystem::Tick(float DeltaTime)
{
	if (!VelocityRT) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// 没有任何激活的 Field → 跳过整个管线，零开销
	if (ActiveFields.Num() == 0) return;

	// 跟随本地 Pawn XY
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			VelocityFieldCenter = Pawn->GetActorLocation();
		}
	}

	// 速度场管线：Clear → Splat
	UKismetRenderingLibrary::ClearRenderTarget2D(this, VelocityRT, LeafFieldSplatParam::ZeroVelocityColor);
	if (SplatMID)
	{
		SplatPass();
	}

	// 推参数给所有激活的 Field
	for (auto It = ActiveFields.CreateIterator(); It; ++It)
	{
		ALeafInteractionField* Field = It->Get();
		if (!Field) { It.RemoveCurrent(); continue; }
		Field->PushDynamicParams(VelocityFieldCenter);
	}
}

// ============================================================
// Splat
// ============================================================

FVector2D ULeafFieldSubsystem::WorldToVelocityUV(const FVector& WorldPos) const
{
	const float HalfW = VelocityFieldWidth * 0.5f;
	return FVector2D(
		(WorldPos.X - (VelocityFieldCenter.X - HalfW)) / VelocityFieldWidth,
		(WorldPos.Y - (VelocityFieldCenter.Y - HalfW)) / VelocityFieldWidth);
}

void ULeafFieldSubsystem::SplatPass()
{
	// 取速度幅度最大的那个 Source 做一次 Splat
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
		if (Mag > BestMag)
		{
			BestMag = Mag;
			BestSrc = Src;
		}
	}

	if (!BestSrc || BestMag < KINDA_SMALL_NUMBER) return;

	const FVector2D SplatUV = WorldToVelocityUV(BestSrc->GetSourceWorldLocation());
	const FVector2D NormVel = BestSrc->GetVelocityXY() * InvVelScale;
	const float     RadiusUV = (VelocityFieldWidth > KINDA_SMALL_NUMBER)
		? (BestSrc->BrushRadiusWorld / VelocityFieldWidth)
		: 0.f;

	SplatMID->SetVectorParameterValue(LeafFieldSplatParam::P_CenterUV,
		FLinearColor(SplatUV.X, SplatUV.Y, 0.f, 0.f));
	SplatMID->SetScalarParameterValue(LeafFieldSplatParam::P_RadiusUV, RadiusUV);
	SplatMID->SetVectorParameterValue(LeafFieldSplatParam::P_Velocity,
		FLinearColor(NormVel.X, NormVel.Y, 0.f, 1.f));

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, VelocityRT, SplatMID);
}
