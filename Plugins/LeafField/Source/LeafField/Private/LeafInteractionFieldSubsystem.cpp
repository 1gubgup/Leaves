// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeafInteractionFieldSubsystem.h"
#include "LeafInteractionSourceComponent.h"

#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

namespace LeafFieldParam
{
	// Splat 材质参数名
	static const FName P_SplatCenterUV(TEXT("SplatCenterUV"));
	static const FName P_SplatRadiusUV(TEXT("SplatRadiusUV"));
	static const FName P_SplatVelocity(TEXT("SplatVelocity"));

	// Niagara User Parameter 名（必须 User. 前缀，且与 Niagara 资产里完全一致）
	static const FName N_VelocityRT(TEXT("User.VelocityRT"));
	static const FName N_CaptureCenter(TEXT("User.CaptureCenter"));
	static const FName N_CaptureWidth(TEXT("User.CaptureWidth"));
	static const FName N_WindStrength(TEXT("User.WindStrength"));
	static const FName N_VerticalLift(TEXT("User.VerticalLift"));
	static const FName N_MaxWindSpeed(TEXT("User.MaxWindSpeed"));
	static const FString N_VelocityRT_Str = N_VelocityRT.ToString();

	// 编码后的"零速度"颜色。解码：NormVel = (Sample.rg - 0.5) * 2
	static const FLinearColor ZeroVelocityColor(0.5f, 0.5f, 0.f, 1.f);
}

// 资产路径常量定义（声明在 .h 里）
const FString ULeafInteractionFieldSubsystem::VelocityRTAssetPath =
	TEXT("/LeafField/LeafField/RT_VelocityField.RT_VelocityField");
const FString ULeafInteractionFieldSubsystem::SplatMaterialPath =
	TEXT("/LeafField/LeafField/M_FluidSplat.M_FluidSplat");

void ULeafInteractionFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ---------- RT ----------
	if (!VelocityRTAssetPath.IsEmpty())
	{
		RT = LoadObject<UTextureRenderTarget2D>(nullptr, *VelocityRTAssetPath);
	}
	if (!RT)
	{
		RT = UKismetRenderingLibrary::CreateRenderTarget2D(
			this, RTSize, RTSize, RTF_RG8, LeafFieldParam::ZeroVelocityColor, /*bAutoGenerateMipMaps=*/false);
	}

	// Clamp 寻址 + 把资产 ClearColor 也设成编码零，避免 BeginDrawCanvasToRenderTarget
	// 的隐式 Clear 把基准盖成黑（曾经的 Bug：人一停 → RT 变黑 → 粒子集体往 -X-Y 飘）。
	if (RT)
	{
		RT->AddressX = TA_Clamp;
		RT->AddressY = TA_Clamp;
		RT->ClearColor = LeafFieldParam::ZeroVelocityColor;
		RT->UpdateResourceImmediate(/*bClearRenderTarget=*/true);
	}

	// ---------- 材质 ----------
	if (!SplatMaterialPath.IsEmpty())
	{
		SplatMaterial = LoadObject<UMaterialInterface>(nullptr, *SplatMaterialPath);
		if (SplatMaterial)
		{
			SplatMID = UMaterialInstanceDynamic::Create(SplatMaterial, this);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[LeafField] Splat material not found: %s"), *SplatMaterialPath);
		}
	}

	// ---------- Niagara 缓存 ----------
	// 不在这里扫：WorldSubsystem 初始化时刻太早，关卡里预放的 Actor 可能还没 Spawn 完。
	// 真正的缓存在 PushToNiagara() 里 lazy 构建（缓存为空就重扫一次），
	// 这样无论 Actor 是关卡预放还是运行时 Spawn 的都能稳定抓到。
	if (UWorld* World = GetWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &ULeafInteractionFieldSubsystem::OnActorSpawned));
	}
}

void ULeafInteractionFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
	}
	ActorSpawnedHandle.Reset();

	CachedNiagaraComponents.Reset();
	Sources.Reset();
	SplatMaterial = nullptr;
	SplatMID = nullptr;
	RT = nullptr;

	Super::Deinitialize();
}

TStatId ULeafInteractionFieldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULeafInteractionFieldSubsystem, STATGROUP_Tickables);
}

void ULeafInteractionFieldSubsystem::RegisterSource(ULeafInteractionSourceComponent* Source)
{
	if (Source) Sources.AddUnique(Source);
}

void ULeafInteractionFieldSubsystem::UnregisterSource(ULeafInteractionSourceComponent* Source)
{
	if (!Source) return;
	Sources.RemoveAll([Source](const TWeakObjectPtr<ULeafInteractionSourceComponent>& W)
	{
		return !W.IsValid() || W.Get() == Source;
	});
}

FVector2D ULeafInteractionFieldSubsystem::WorldToUV(const FVector& WorldPos) const
{
	const float HalfW = CaptureWidth * 0.5f;
	const float U = (WorldPos.X - (CaptureCenter.X - HalfW)) / CaptureWidth;
	const float V = (WorldPos.Y - (CaptureCenter.Y - HalfW)) / CaptureWidth;
	return FVector2D(U, V);
}

void ULeafInteractionFieldSubsystem::Tick(float DeltaTime)
{
	if (!RT) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// 限频：UpdateRateHz > 0 时按固定频率更新（手机端默认 30Hz，省一半 GPU/带宽）。
	// Niagara 端的力是 lerp，30Hz 风场 + 60Hz 粒子在视觉上看不出差。
	if (UpdateRateHz > KINDA_SMALL_NUMBER)
	{
		TickAccumulator += DeltaTime;
		const float Interval = 1.f / UpdateRateHz;
		if (TickAccumulator < Interval) return;
		// 减去 Interval 而不是清零，避免长帧丢更新（最多累 2 帧避免突发卡顿后追帧）
		TickAccumulator = FMath::Min(TickAccumulator - Interval, Interval);
	}

	// CaptureCenter 跟随本地主角
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			CaptureCenter = Pawn->GetActorLocation();
		}
	}

	// 每帧清成"编码零"，再叠 Splat（无尾流）
	UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, LeafFieldParam::ZeroVelocityColor);

	if (SplatMID)
	{
		SplatPass();
	}

	PushToNiagara();
}

void ULeafInteractionFieldSubsystem::SplatPass()
{
	// ----------------------------------------------------------------------------------------
	// 「取最大」策略 + DrawMaterialToRenderTarget 一次全屏写入。
	//
	// 为什么这么写：
	//  1) UE5.7 mobile 渲染路径下，FCanvas::DrawTile → FRendererModule::DrawTileMesh 会触发
	//     RenderGraphValidation.cpp 的 checkf 断言（SinglePrimitiveInstanceTextureView.Upload
	//     Pass 缺 NeverCull 标记），Android Development 包必崩。r.RDG.Debug 是 cvar，无法关掉
	//     编译期 checkf。唯一根治方式：从代码层移除 FCanvas 路径，改走 DrawMaterialToRenderTarget。
	//  2) 取最大：当前风场仅 5m，多 Source（主角 + 武器 + BOSS）高度重叠，挑 |V| 最大者写入即可，
	//     视觉上等价于"跑得快的盖过跑得慢的"，O(N) 选举 + 1 次全屏 Pass，比逐 Source DrawTile
	//     在 N≥2 时还省。
	//
	// 升级钩子：未来若需要真正叠加多个 Source（而不是取最大），把这里换成 Ping-Pong 双 RT：
	//     - 每帧拿前一帧 RT 当输入纹理（或本帧 RT_Temp）；
	//     - 对每个 Source 用一个"读旧值 + 写新值"的累加材质 DrawMaterialToRenderTarget；
	//     - 保留 ClearRenderTarget2D 在 Tick 顶端做基准重置（或改成不清，做拖尾）。
	//   头文件接口、Niagara 端、RT 资产都不需要改，只在 SplatPass 内部替换。
	// ----------------------------------------------------------------------------------------

	if (!SplatMID || !RT || Sources.Num() == 0) return;

	const float InvVelScale = (VelocityScale > KINDA_SMALL_NUMBER) ? (1.f / VelocityScale) : 1.f;

	FVector2D WinUV(0.f, 0.f);
	FVector2D WinNormVel(0.f, 0.f);
	float     WinRadius = 0.f;
	float     WinMagSq  = -1.f; // < 0 表示尚无赢家

	// O(N) 选举：剔除 UV 出界的 Source，比较 |NormVel|² 取最大
	for (auto It = Sources.CreateIterator(); It; ++It)
	{
		ULeafInteractionSourceComponent* Src = It->Get();
		if (!Src)
		{
			It.RemoveCurrent();
			continue;
		}

		const FVector2D UV = WorldToUV(Src->GetSourceWorldLocation());
		if (UV.X < -0.1f || UV.X > 1.1f || UV.Y < -0.1f || UV.Y > 1.1f) continue;

		// NormVel ∈ ~[-1,1]，材质内部完成 *0.5+0.5 编码
		const FVector2D NormVel = Src->GetVelocityXY() * InvVelScale;
		const float MagSq = NormVel.SizeSquared();

		if (MagSq > WinMagSq)
		{
			WinMagSq    = MagSq;
			WinUV       = UV;
			WinNormVel  = NormVel;
			WinRadius   = Src->BrushRadiusUV;
		}
	}

	// 全部出界 / 全部失效 → 直接 return：Tick 顶端的 ClearRenderTarget2D 已把 RT 清成编码零，
	// 不需要再走一次全屏 Pass，省一次绘制。
	if (WinMagSq < 0.f) return;

	// 写赢家参数到 MID，然后一次全屏绘制
	SplatMID->SetVectorParameterValue(LeafFieldParam::P_SplatCenterUV,
		FLinearColor(WinUV.X, WinUV.Y, 0.f, 0.f));
	SplatMID->SetScalarParameterValue(LeafFieldParam::P_SplatRadiusUV, WinRadius);
	SplatMID->SetVectorParameterValue(LeafFieldParam::P_SplatVelocity,
		FLinearColor(WinNormVel.X, WinNormVel.Y, 0.f, 1.f));

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, RT, SplatMID);
}

void ULeafInteractionFieldSubsystem::PushToNiagara()
{
	if (!RT) return;

	// Lazy 缓存：第一次需要用且缓存空时才扫一次（之后 OnActorSpawned 增量维护）。
	// 关卡里 Niagara Actor 是预放的，不会触发 OnActorSpawned，所以这里兜底。
	if (CachedNiagaraComponents.Num() == 0)
	{
		RebuildNiagaraCache();
	}

	for (auto It = CachedNiagaraComponents.CreateIterator(); It; ++It)
	{
		UNiagaraComponent* NC = It->Get();
		if (!NC)
		{
			It.RemoveCurrent();
			continue;
		}

		UNiagaraFunctionLibrary::SetTextureObject(NC, LeafFieldParam::N_VelocityRT_Str, RT);
		NC->SetVariableVec3(LeafFieldParam::N_CaptureCenter, CaptureCenter);
		NC->SetVariableFloat(LeafFieldParam::N_CaptureWidth, CaptureWidth);
		NC->SetVariableFloat(LeafFieldParam::N_WindStrength, WindStrength);
		NC->SetVariableFloat(LeafFieldParam::N_VerticalLift, VerticalLift);
		NC->SetVariableFloat(LeafFieldParam::N_MaxWindSpeed, MaxWindSpeed);
	}

	if (PushLogIntervalFrames > 0)
	{
		static int32 LogCounter = 0;
		if ((LogCounter++ % PushLogIntervalFrames) == 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[LeafField] Push: NCs=%d, Center=(%.0f,%.0f,%.0f), Width=%.0f"),
				CachedNiagaraComponents.Num(), CaptureCenter.X, CaptureCenter.Y, CaptureCenter.Z, CaptureWidth);
		}
	}
}

void ULeafInteractionFieldSubsystem::RebuildNiagaraCache()
{
	CachedNiagaraComponents.Reset();

	UWorld* World = GetWorld();
	if (!World) return;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		OnActorSpawned(*It);
	}
}

void ULeafInteractionFieldSubsystem::OnActorSpawned(AActor* InActor)
{
	if (!InActor || !InActor->ActorHasTag(LeafFieldActorTag)) return;

	TArray<UNiagaraComponent*> NCs;
	InActor->GetComponents<UNiagaraComponent>(NCs);
	for (UNiagaraComponent* NC : NCs)
	{
		if (NC) CachedNiagaraComponents.AddUnique(NC);
	}
}
