// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeafInteractionField.h"
#include "LeafFieldSubsystem.h"
#include "LeafFieldSettings.h"

#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetRenderingLibrary.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeafField, Log, All);

// ============================================================
// Niagara User Parameter 名（与 N_LeafField 资产一一对应）
// ============================================================
namespace LeafFieldNiagara
{
	// Subsystem 推
	static const FString N_VelocityRT_S         = TEXT("User.VelocityRT");
	static const FName   N_VelocityFieldCenter (TEXT("User.VelocityFieldCenter"));
	static const FName   N_VelocityFieldWidth  (TEXT("User.VelocityFieldWidth"));
	static const FName   N_WindStrength        (TEXT("User.WindStrength"));
	static const FName   N_WindLift            (TEXT("User.WindLift"));
	static const FName   N_WindMaxSpeed        (TEXT("User.WindMaxSpeed"));
	static const FName   N_WindResponseSpeed   (TEXT("User.WindResponseSpeed"));
	static const FName   N_WindSpinImpulse     (TEXT("User.WindSpinImpulse"));

	// Field 推
	static const FString N_HeightRT_S           = TEXT("User.HeightRT");
	static const FName   N_HeightCaptureZ      (TEXT("User.HeightCaptureZ"));
	static const FName   N_FieldOrigin         (TEXT("User.FieldOrigin"));
	static const FName   N_FieldExtent         (TEXT("User.FieldExtent"));
	static const FName   N_GroundOffset        (TEXT("User.GroundOffset"));
	static const FName   N_GroundBlendHeight   (TEXT("User.GroundBlendHeight"));

	// 美术暴露
	static const FName   N_LeafCount           (TEXT("User.LeafCount"));

	// 多 Mesh 槽位（最多 4 种）→ Niagara Mesh Renderer 的 4 个 Mesh slot
	static const FName   N_LeafMesh0           (TEXT("User.LeafMesh0"));
	static const FName   N_LeafMesh1           (TEXT("User.LeafMesh1"));
	static const FName   N_LeafMesh2           (TEXT("User.LeafMesh2"));
	static const FName   N_LeafMesh3           (TEXT("User.LeafMesh3"));
	// Mesh 选择累积阈值（归一化到 [0,1]）打包成单个 Vector3：
	//   X/Y/Z = 槽 0/1/2 的累积上限；槽 3 为隐式兜底（RandMesh ≥ Z 时选 3）。
	//   合并成一个参数是为了复用与 FieldOrigin 相同的 Vec3 绑定路径，避免 3 个
	//   分散 Float 参数名字对不齐导致两端都读到默认 0。
	static const FName   N_MeshThresholds      (TEXT("User.MeshThresholds"));

	static const FName MeshParamNames[4] = {
		N_LeafMesh0, N_LeafMesh1, N_LeafMesh2, N_LeafMesh3
	};

	// Renderer 槽位数（与 N_LeafField 资产里的 Mesh slot 数一致）
	static constexpr int32 MaxMeshSlots = 4;
}

// ============================================================
// 构造
// ============================================================

ALeafInteractionField::ALeafInteractionField()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FieldBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FieldBox"));
	FieldBox->SetupAttachment(SceneRoot);
	FieldBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FieldBox->ShapeColor = FColor(255, 200, 0);
	FieldBox->SetLineThickness(2.f);

	NiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Niagara"));
	NiagaraComponent->SetupAttachment(SceneRoot);
	NiagaraComponent->bAutoActivate = false;

	HeightCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("HeightCapture"));
	HeightCapture->SetupAttachment(SceneRoot);
	HeightCapture->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	HeightCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	HeightCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	HeightCapture->bCaptureEveryFrame = false;
	HeightCapture->bCaptureOnMovement = false;
	HeightCapture->bAlwaysPersistRenderingState = true;
	// 关掉无关渲染特性，降低高度捕获开销
	HeightCapture->ShowFlags.SetTemporalAA(false);
	HeightCapture->ShowFlags.SetMotionBlur(false);
	HeightCapture->ShowFlags.SetEyeAdaptation(false);
	HeightCapture->ShowFlags.SetBloom(false);
	HeightCapture->ShowFlags.SetLensFlares(false);

	// 用 SyncBoxesToParams 统一初始化所有与尺寸相关的值，保证构造阶段和运行时一致
	SyncBoxesToParams();
}

// ============================================================
// 生命周期
// ============================================================

void ALeafInteractionField::BeginPlay()
{
	Super::BeginPlay();

	SyncBoxesToParams();

	if (NiagaraComponent && LeafSystem)
	{
		NiagaraComponent->SetAsset(LeafSystem);
	}
	else if (!LeafSystem)
	{
		UE_LOG(LogLeafField, Warning,
			TEXT("[LeafField] %s has no LeafSystem assigned. Field will be inert."),
			*GetName());
	}

	// 一开始就全部启动：默认 BeginPlay 自动激活，无需外部调用 ActivateField()
	if (bAutoActivateOnBeginPlay)
	{
		ActivateField();
	}
}

void ALeafInteractionField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (ULeafFieldSubsystem* Sub = World->GetSubsystem<ULeafFieldSubsystem>())
		{
			Sub->NotifyFieldDeactivated(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ALeafInteractionField::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SyncBoxesToParams();
}
#endif

void ALeafInteractionField::SyncBoxesToParams()
{
	if (FieldBox)
	{
		FieldBox->SetBoxExtent(FieldExtent);
	}
	if (HeightCapture)
	{
		HeightCapture->SetRelativeLocation(FVector(0.f, 0.f, HeightCaptureZOffset));
		HeightCapture->OrthoWidth = FMath::Max(FieldExtent.X, FieldExtent.Y) * 2.f;
		bHeightCaptured = false;
	}
}

// ============================================================
// 高度图
// ============================================================

void ALeafInteractionField::EnsureHeightCaptured()
{
	if (!HeightCapture) return;

	// HeightRT 只需创建一次，格式 R16f：移动端线性过滤安全、精度足够（~65000cm @ 1cm 分辨率）
	if (!HeightRT)
	{
		const ULeafFieldSettings* S = GetDefault<ULeafFieldSettings>();
		const int32 RTSize = S ? S->HeightRTSize : 256;
		HeightRT = UKismetRenderingLibrary::CreateRenderTarget2D(
			this, RTSize, RTSize, RTF_R16f);
		if (HeightRT)
		{
			HeightRT->AddressX = TA_Clamp;
			HeightRT->AddressY = TA_Clamp;
		}
	}

	if (!HeightRT)
	{
		UE_LOG(LogLeafField, Warning, TEXT("[LeafField] %s: failed to create HeightRT"), *GetName());
		return;
	}

	// 地形静态：首次激活时拍一次即可，后续激活复用已有内容，避免重复 SceneCapture 开销
	if (bHeightCaptured) return;

	HeightCapture->TextureTarget = HeightRT;
	HeightCapture->OrthoWidth = FMath::Max(FieldExtent.X, FieldExtent.Y) * 2.f;

	// 高度图只应记录静态地形。拍摄前把场景里所有 Pawn（玩家角色 / NPC 等动态物体）
	// 加入隐藏列表，避免其深度被烤进高度图，导致叶子把"人头顶"当成地面而贴上去。
	if (UWorld* World = GetWorld())
	{
		HeightCapture->HiddenActors.Reset();
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			HeightCapture->HiddenActors.Add(*It);
		}
	}

	HeightCapture->CaptureScene();
	bHeightCaptured = true;
}

// ============================================================
// 激活/休眠
// ============================================================

void ALeafInteractionField::ActivateField()
{
	if (State == ELeafFieldState::Active) return;

	if (!NiagaraComponent || !LeafSystem)
	{
		UE_LOG(LogLeafField, Warning, TEXT("[LeafField] %s ActivateField failed: NiagaraComponent=%d LeafSystem=%d"),
			*GetName(), NiagaraComponent != nullptr, LeafSystem != nullptr);
		return;
	}

	UE_LOG(LogLeafField, Log, TEXT("[LeafField] %s ActivateField called"), *GetName());

	EnsureHeightCaptured();

	// Niagara System 可能还在运行时编译（首次使用时触发），
	// 编译期间 Activate 的 Spawn Burst 会被跳过，监听编译完成后再重新 Activate。
	if (LeafSystem->HasOutstandingCompilationRequests())
	{
		UE_LOG(LogLeafField, Warning, TEXT("[LeafField] %s: NiagaraSystem still compiling, deferring Activate"), *GetName());
		LeafSystem->OnSystemCompiled().AddUObject(this, &ALeafInteractionField::OnNiagaraCompiled);
		// 先把 State 和 Subsystem 注册设好，等编译完回调里再真正 Activate
		State = ELeafFieldState::Active;
		if (UWorld* World = GetWorld())
		{
			if (ULeafFieldSubsystem* Sub = World->GetSubsystem<ULeafFieldSubsystem>())
			{
				Sub->NotifyFieldActivated(this);
			}
		}
		return;
	}

	// 先推参数（含 LeafCount / FieldExtent），再 Activate，确保 Spawn Burst 读到正确值
	State = ELeafFieldState::Active;
	if (UWorld* World = GetWorld())
	{
		if (ULeafFieldSubsystem* Sub = World->GetSubsystem<ULeafFieldSubsystem>())
		{
			Sub->NotifyFieldActivated(this);
			// 激活前立即推一次参数，确保 Spawn Burst 读到正确值
			PushStaticParams();
			PushDynamicParams(Sub->GetVelocityFieldCenter());
		}
	}

	NiagaraComponent->Activate(true);  // 参数就绪后再触发 Spawn Burst
}

void ALeafInteractionField::OnNiagaraCompiled(UNiagaraSystem* InSystem)
{
	if (InSystem)
	{
		InSystem->OnSystemCompiled().RemoveAll(this);
	}

	if (NiagaraComponent && State == ELeafFieldState::Active)
	{
		UE_LOG(LogLeafField, Log, TEXT("[LeafField] %s: NiagaraSystem compiled, activating now"), *GetName());

		if (UWorld* World = GetWorld())
		{
			if (ULeafFieldSubsystem* Sub = World->GetSubsystem<ULeafFieldSubsystem>())
			{
				// 编译完成，立即推一次参数
				PushStaticParams();
				PushDynamicParams(Sub->GetVelocityFieldCenter());
			}
		}

		NiagaraComponent->Activate(true);  // 再触发 Spawn Burst
	}
}

void ALeafInteractionField::DeactivateField()
{
	if (State == ELeafFieldState::Dormant) return;

	if (NiagaraComponent)
	{
		NiagaraComponent->Deactivate();
	}
	State = ELeafFieldState::Dormant;

	if (UWorld* World = GetWorld())
	{
		if (ULeafFieldSubsystem* Sub = World->GetSubsystem<ULeafFieldSubsystem>())
		{
			Sub->NotifyFieldDeactivated(this);
		}
	}
}

// ============================================================
// 参数推送
// ============================================================

void ALeafInteractionField::PushStaticParams()
{
	if (!NiagaraComponent || State != ELeafFieldState::Active) return;

	UWorld* World = GetWorld();
	ULeafFieldSubsystem* Sub = World ? World->GetSubsystem<ULeafFieldSubsystem>() : nullptr;

	// --- Subsystem 全局静态（初始化后不变）---
	if (Sub)
	{
		if (UTextureRenderTarget2D* VRT = Sub->GetVelocityRT())
		{
			UNiagaraFunctionLibrary::SetTextureObject(NiagaraComponent,
				LeafFieldNiagara::N_VelocityRT_S, VRT);
		}
		NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_VelocityFieldWidth, Sub->GetVelocityFieldWidth());
		NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_WindMaxSpeed,       Sub->GetWindMaxSpeed());
	}

	// --- Wind（per-Field，每个 Actor 独立）---
	// WindMaxSpeed 来自全局（速度场 RG8 编码基准，编解码必须匹配，见上方 Sub 分支）
	NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_WindStrength,      WindStrength);
	NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_WindLift,          WindLift);
	NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_WindResponseSpeed, WindResponseSpeed);
	NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_WindSpinImpulse,   WindSpinImpulse);

	// --- Field 自身 ---
	const FVector FieldOrigin = GetActorLocation();
	const float HeightCaptureWorldZ = FieldOrigin.Z + HeightCaptureZOffset;
	if (HeightRT)
	{
		UNiagaraFunctionLibrary::SetTextureObject(NiagaraComponent,
			LeafFieldNiagara::N_HeightRT_S, HeightRT);
	}
	NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_HeightCaptureZ,    HeightCaptureWorldZ);
	NiagaraComponent->SetVariableVec3 (LeafFieldNiagara::N_FieldOrigin,       FieldOrigin);
	NiagaraComponent->SetVariableVec3 (LeafFieldNiagara::N_FieldExtent,       FieldExtent);
	NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_GroundOffset,      GroundOffset);
	NiagaraComponent->SetVariableFloat(LeafFieldNiagara::N_GroundBlendHeight, GroundBlendHeight);

	// --- 美术暴露 ---
	NiagaraComponent->SetVariableInt(LeafFieldNiagara::N_LeafCount, LeafCount);

	// ── 多 Mesh + 权重（固定 4 槽）──────────────────────────────────
	// 1) 收集有效权重：Mesh 为空的槽位权重强制 0；非空但权重 ≤0 时按 1 兜底
	constexpr int32 N = LeafFieldNiagara::MaxMeshSlots;
	float SlotWeights[N] = { 0.f };
	float TotalWeight = 0.f;
	for (int32 i = 0; i < N; ++i)
	{
		if (LeafMeshes[i].Mesh == nullptr) { continue; }
		const float W = (LeafMeshes[i].Weight > 0) ? (float)LeafMeshes[i].Weight : 1.f;
		SlotWeights[i] = W;
		TotalWeight += W;
	}

	// 2) 推累积阈值（归一化），打包进 Vector3 的 XYZ = 槽 0/1/2 累积上限。
	//    无任何有效 Mesh 时阈值全 0，Spawn 会回退到兜底槽位。
	FVector Thresholds = FVector::ZeroVector;
	float Cumulative = 0.f;
	for (int32 i = 0; i < N; ++i)
	{
		if (TotalWeight > 0.f) { Cumulative += SlotWeights[i] / TotalWeight; }
		const float Clamped = FMath::Min(Cumulative, 1.f);
		if      (i == 0) { Thresholds.X = Clamped; }
		else if (i == 1) { Thresholds.Y = Clamped; }
		else if (i == 2) { Thresholds.Z = Clamped; }
		// i == 3 为隐式兜底，HLSL 不读取
	}
	NiagaraComponent->SetVariableVec3(LeafFieldNiagara::N_MeshThresholds, Thresholds);

	// [临时调试] 确认 C++ 端算出并推送的阈值；定位完成后可删除此行。
	UE_LOG(LogLeafField, Warning,
		TEXT("[LeafField] %s PushThresholds TotalWeight=%.2f Thresholds=(%.3f, %.3f, %.3f)"),
		*GetName(), TotalWeight, Thresholds.X, Thresholds.Y, Thresholds.Z);

	// 3) 推 Mesh：空槽位用"上一个有效 Mesh"填充，避免 Niagara Renderer 拿到 null。
	//    这些填充槽因权重为 0、阈值不增长，永远不会被粒子选中，纯防御性占位。
	UStaticMesh* LastValid = nullptr;
	for (int32 i = 0; i < N; ++i)
	{
		if (LeafMeshes[i].Mesh) { LastValid = LeafMeshes[i].Mesh.Get(); }
		if (LastValid)
		{
			NiagaraComponent->SetVariableStaticMesh(
				LeafFieldNiagara::MeshParamNames[i], LastValid);
		}
	}
}

void ALeafInteractionField::PushDynamicParams(const FVector& VelocityFieldCenter)
{
	if (!NiagaraComponent || State != ELeafFieldState::Active) return;
	NiagaraComponent->SetVariableVec3(LeafFieldNiagara::N_VelocityFieldCenter, VelocityFieldCenter);
}

void ALeafInteractionField::RefreshParams()
{
	PushStaticParams();
}

#if WITH_EDITOR
void ALeafInteractionField::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncBoxesToParams();
	if (State == ELeafFieldState::Active)
	{
		PushStaticParams();
	}
}
#endif
