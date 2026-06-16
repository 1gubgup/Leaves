// Copyright Epic Games, Inc. All Rights Reserved.

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

// ─── 材质参数名 ────────────────────────────────────────────────────────────────
namespace LeafFieldSplatParam
{
	static const FName P_CenterUV(TEXT("SplatCenterUV"));
	static const FName P_RadiusUV(TEXT("SplatRadiusUV"));
	static const FName P_Velocity(TEXT("SplatVelocity"));

	// RG8 零速编码：(0.5, 0.5, 0, 1)，解码 NormVel = (rg - 0.5) * 2
	static const FLinearColor ZeroVelocityColor(0.5f, 0.5f, 0.f, 1.f);
}

// ─── 资产路径 ──────────────────────────────────────────────────────────────────
// Content/LeafField/ 下的三个资产，UE 资产引用格式：/插件名/目录/资产名.资产名
const FString ULeafFieldSubsystem::VelocityRTAssetPath =
	TEXT("/LeafField/LeafField/RT_VelocityField.RT_VelocityField");
const FString ULeafFieldSubsystem::SplatMaterialPath =
	TEXT("/LeafField/LeafField/M_FluidSplat.M_FluidSplat");
const FString ULeafFieldSubsystem::NPCAssetPath =
	TEXT("/LeafField/LeafField/NPC_LeafField.NPC_LeafField");

// ─── 生命周期 ──────────────────────────────────────────────────────────────────

void ULeafFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 编辑器世界（PreviewScene、Thumbnail 等）跳过所有初始化。
	// NiagaraWorldManager 仅在 GameWorld 中存在，非 GameWorld 调用会空指针崩溃。
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	// ── 读项目设置 ─────────────────────────────────────────────────
	const ULeafFieldSettings* S = GetDefault<ULeafFieldSettings>();
	if (S)
	{
		VelocityFieldWidth = S->VelocityFieldWidth;
		WindMaxSpeed       = S->WindMaxSpeed;
	}

	// ── 速度场 RT：优先用磁盘资产，否则代码创建 ───────────────────
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
		VelocityRT->AddressX  = TA_Clamp;
		VelocityRT->AddressY  = TA_Clamp;
		VelocityRT->ClearColor = LeafFieldSplatParam::ZeroVelocityColor;
		VelocityRT->UpdateResourceImmediate(true);
	}

	// ── Splat 材质 ────────────────────────────────────────────────
	SplatMaterial = LoadObject<UMaterialInterface>(nullptr, *SplatMaterialPath);
	if (SplatMaterial)
	{
		SplatMID = UMaterialInstanceDynamic::Create(SplatMaterial, this);
	}
	else
	{
		UE_LOG(LogLeafField, Warning, TEXT("[LeafField] Splat material not found: %s"), *SplatMaterialPath);
	}

	// 注意：NPC 实例的获取延迟到首帧 Tick（见 EnsureNPCInstance）。
	// Subsystem::Initialize 时 NiagaraWorldManager 对当前 World 可能尚未注册完成，
	// 此时调用 GetNiagaraParameterCollection 会读空指针崩溃。
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
	if (bNPCResolved) return;          // 只尝试一次
	bNPCResolved = true;

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;

	UNiagaraParameterCollection* NPC = LoadObject<UNiagaraParameterCollection>(nullptr, *NPCAssetPath);
	if (!NPC)
	{
		UE_LOG(LogLeafField, Log,
			TEXT("[LeafField] NPC_LeafField not found at '%s', create it to enable WindInteraction module support"),
			*NPCAssetPath);
		return;
	}

	NPCInstance = UNiagaraFunctionLibrary::GetNiagaraParameterCollection(World, NPC);
	if (NPCInstance)
	{
		NPCInstance->SetFloatParameter(TEXT("VelocityFieldWidth"), VelocityFieldWidth);
		NPCInstance->SetFloatParameter(TEXT("WindMaxSpeed"),       WindMaxSpeed);
		UE_LOG(LogLeafField, Log, TEXT("[LeafField] NPC initialized (Width=%.0f MaxSpeed=%.0f)"),
			VelocityFieldWidth, WindMaxSpeed);
	}
}

void ULeafFieldSubsystem::Tick(float DeltaTime)
{
	if (!VelocityRT) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// 延迟到首帧获取 NPC 实例（此时 NiagaraWorldManager 已就绪）
	EnsureNPCInstance();

	// 速度场跟随本地 Pawn XY
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			VelocityFieldCenter = Pawn->GetActorLocation();
		}
	}

	// Clear → Splat（无 Source 时跳过，RT 保持零速）
	if (Sources.Num() > 0 && SplatMID)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, VelocityRT, LeafFieldSplatParam::ZeroVelocityColor);
		SplatPass();
	}

	// 每帧写入 VelocityFieldCenter，供 WindInteraction 模块读取
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

	// 选速度最大的 Source 做 Splat
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

	const FVector2D SplatUV  = WorldToVelocityUV(BestSrc->GetSourceWorldLocation());
	const FVector2D NormVel  = BestSrc->GetVelocityXY() * InvVelScale;
	const float     RadiusUV = (VelocityFieldWidth > KINDA_SMALL_NUMBER)
		? (BestSrc->BrushRadiusWorld / VelocityFieldWidth) : 0.f;

	SplatMID->SetVectorParameterValue(LeafFieldSplatParam::P_CenterUV,
		FLinearColor(SplatUV.X, SplatUV.Y, 0.f, 0.f));
	SplatMID->SetScalarParameterValue(LeafFieldSplatParam::P_RadiusUV, RadiusUV);
	SplatMID->SetVectorParameterValue(LeafFieldSplatParam::P_Velocity,
		FLinearColor(NormVel.X, NormVel.Y, 0.f, 1.f));

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, VelocityRT, SplatMID);
}
