// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LeafInteractionField.generated.h"

class UBoxComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMesh;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/** Field 状态机 */
UENUM(BlueprintType)
enum class ELeafFieldState : uint8
{
	Dormant,
	Active,
};

/**
 * 单个叶片网格槽位（Mesh + 出现权重）。
 * 美术工作流：往列表里加几项就有几种叶子混撒，每项填一个 Mesh，
 * 权重默认 1，可改为整数比例（无需手动归一化）。
 */
USTRUCT(BlueprintType)
struct FLeafMeshEntry
{
	GENERATED_BODY()

	/** 叶片网格。留空则此槽位不参与渲染与随机（权重自动锁 0、不可编辑）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/**
	 * 出现权重（整数比例，无需手动归一化）。
	 * 仅在 Mesh 非空时可调；Mesh 为空时强制视为 0，不参与随机分配。
	 * 例：三项权重 [2, 1, 1] → 出现概率 50% / 25% / 25%。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField",
		meta = (ClampMin = "0", EditCondition = "Mesh != nullptr"))
	int32 Weight = 1;
};

/**
 * 可拖入关卡的地面叶子交互 Field。
 * 通过 ActivateField() / DeactivateField() 控制激活，可由关卡蓝图、GameMode 或任何外部逻辑调用。
 *
 * ── 暴露属性一览 ─────────────────────────────────────────────────
 *
 *  [LeafField|Asset]
 *    LeafSystem      – 使用的 Niagara System（必填，指向 N_LeafField）
 *    LeafMeshes      – 叶片网格槽位（固定 4 个，每项 = Mesh + Weight）
 *                      → User.LeafMesh0~3 / User.MeshThreshold0~3
 *
 *  [LeafField|Layout]
 *    FieldExtent     – 叶子铺设半尺寸 cm（XY=撒布半径，Z=FieldBox 可视化高度）
 *
 *  [LeafField|Appearance]
 *    LeafCount       – Spawn Burst 数量 → User.LeafCount
 *    GroundOffset    – 叶片贴地安全距离 cm → User.GroundOffset
 *
 *  [LeafField|Wind]  （每个 Field 独立，全局基准见项目设置 → Leaf Field）
 *    WindStrength      – 本 Field 风强度倍率（0~3），默认 1.0
 *    WindLift          – 水平风转上抬力比例（0~1），默认 0.05
 *    WindResponseSpeed – 叶子跟随风场的响应速度（1/s），默认 8.0；越大越灵敏，帧率无关
 *    WindSpinImpulse   – 起飞旋转冲量强度（0~5），默认 0.5
 *
 *  [LeafField|Advanced]
 *    HeightCaptureZOffset – 高度相机在 Actor 原点上方的拍摄高度 cm
 *    GroundBlendHeight    – 贴地过渡区高度 cm（姿态对齐 + 旋转阻尼双重阈值）
 * ────────────────────────────────────────────────────────────────
 */
UCLASS(Blueprintable,
	HideCategories = (Rendering, Mobile, Replication, Input, Actor, Tags,
	                  AssetUserData, LOD, Cooking, Activation, Physics,
	                  Navigation, HLOD, RayTracing, VirtualTexture,
	                  Lighting, Collision))
class LEAFFIELD_API ALeafInteractionField : public AActor
{
	GENERATED_BODY()

public:
	ALeafInteractionField();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	/** Details 面板里改属性时自动热推参数，无需停止 PIE */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ============================================================
	// 美术参数 – Asset
	// ============================================================

	/** 使用哪个 Niagara System，必须指向 N_LeafField 或其子系统 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Asset")
	TObjectPtr<UNiagaraSystem> LeafSystem = nullptr;

	/**
	 * 勾选后 Actor BeginPlay 时自动激活叶子 Field（生成粒子、开始每帧推参），
	 * 无需外部蓝图/代码调用 ActivateField()。
	 * 取消勾选则保持休眠，由外部逻辑决定何时调用 ActivateField()。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Asset")
	bool bAutoActivateOnBeginPlay = true;

	/**
	 * 叶片网格槽位（固定 4 个，对应 Niagara Mesh Renderer 的 4 个槽位）。
	 * 每个槽位含一个 Mesh 和一个出现权重；留空的槽位不参与随机，权重自动视为 0。
	 * 填入 Mesh 后调整 Weight 比例即可，无需手动归一化
	 * （例：三个槽权重 [2, 1, 1] → 出现概率 50% / 25% / 25%）。
	 */
	// 固定 4 个槽位（UHT 不支持将静态数组暴露给蓝图，故仅保留 EditAnywhere；
	// 如需蓝图访问，请通过 C++ 封装函数或 BlueprintCallable Getter 转发）。
	UPROPERTY(EditAnywhere, Category = "LeafField|Asset",
		meta = (TitleProperty = "Mesh"))
	FLeafMeshEntry LeafMeshes[4];

	// ============================================================
	// 美术参数 – Layout
	// ============================================================

	/**
	 * 叶子铺设区域的半尺寸（cm）。
	 *   XY：撒布半径，实际铺设面积 = X×2 × Y×2（例如 500 → 10m）
	 *   Z ：FieldBox 碰撞高度（极小即可，仅用于可视化框）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Layout",
		meta = (ForceUnits = "cm"))
	FVector FieldExtent = FVector(500.f, 500.f, 10.f);

	// ============================================================
	// 美术参数 – Appearance
	// ============================================================

	/** Spawn Burst 数量，推送到 User.LeafCount */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Appearance",
		meta = (ClampMin = "0", ClampMax = "65536"))
	int32 LeafCount = 1024;

	/** 叶片贴地安全距离（cm），推送到 User.GroundOffset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Appearance",
		meta = (ClampMin = "0.0", ClampMax = "50.0", ForceUnits = "cm"))
	float GroundOffset = 5.f;

	// ============================================================
	// 美术参数 – Wind（每个 Field 独立）
	// ============================================================

	/**
	 * 本 Field 的风强度倍率。
	 * 解码后的速度场速度乘以此值再驱动叶子，可让密林/开阔地有不同风感。
	 * 1.0 = 正常；0 = 完全无风；2.0 = 双倍风强。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Wind",
		meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float WindStrength = 1.0f;

	/**
	 * 水平风速转化为垂直上抬力的比例（0~1）。
	 * 0 = 叶子只水平飘，不会被吹起；1 = 上抬力与水平风等强。
	 * 堆在凹地的叶子建议调低，开阔地的叶子建议适当调高。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Wind",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindLift = 0.1f;

	/**
	 * 叶子跟随风场速度的响应速度（1/s，即帧率无关的一阶低通截止频率）。
	 * Alpha = saturate(DeltaTime * WindResponseSpeed)，值越大叶子越灵敏。
	 *   1  → 约 1 秒内完全响应（拖拽感强）
	 *   8  → 约 0.12 秒完全响应（推荐默认）
	 *   20 → 约 0.05 秒，几乎硬跟随
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Wind",
		meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float WindResponseSpeed = 8.0f;

	/**
	 * 叶子被风"踢起"时施加的旋转冲量强度。
	 * 0 = 被风吹动时叶子不翻滚；越大越剧烈。
	 * 对应 HLSL 里 kickRatio（速度增量驱动）乘以此值后写入 OutRotationalVelocity。
	 * 注意：冲量 Z 分量幅值最大为 2×WindSpinImpulse，翻滚过强时建议调至 0.5~1.0。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Wind",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float WindSpinImpulse = 0.5f;

	// ============================================================
	// 技术参数（一般保持默认，需要时才调整）
	// ============================================================

	/**
	 * 高度拍摄相机在 Actor 原点上方的高度（cm）。
	 * 相机从这里朝下正交拍摄，第一个遮挡面即为"地面"高度。
	 * 默认 2000cm（20m）适合绝大多数平坦地形。
	 *
	 * ⚠️ 最小安全值 = 场地内地形最高点相对 Actor 原点的高差 + 余量。
	 *    若设置值小于此高差，相机会嵌入地形，深度图全黑，叶子无法贴地。
	 *    有桥洞/山洞等低于 Actor 原点的地面时可适当调高；
	 *    有树冠等悬空遮挡时需确保相机在遮挡物下方，或直接调低。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Advanced",
		meta = (ClampMin = "100.0", ClampMax = "10000.0", ForceUnits = "cm"))
	float HeightCaptureZOffset = 2000.f;

	/**
	 * 贴地过渡区高度（cm）。
	 * 叶子距地面低于此值时开始对齐地形法线并阻尼旋转速度；距地越近效果越强。
	 * 向上飞行时此逻辑完全不介入（由速度方向权重保证）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeafField|Advanced",
		meta = (ClampMin = "1.0", ClampMax = "100.0", ForceUnits = "cm"))
	float GroundBlendHeight = 10.0f;

	// ============================================================
	// 公开接口
	// ============================================================

	/** 由 Subsystem 每帧调用，推唯一逐帧变化的参数：速度场跟随中心。*/
	void PushDynamicParams(const FVector& VelocityFieldCenter);

	/**
	 * 将所有静态参数（Wind / Mesh / FieldExtent 等）推给 Niagara，ActivateField() 时自动调用一次。
	 * 在编辑器 Details 面板修改属性时会自动触发（PostEditChangeProperty）；
	 * 在蓝图或代码里运行时修改属性后也可手动调用，无需重新激活 Field。
	 */
	UFUNCTION(BlueprintCallable, Category = "LeafField")
	void RefreshParams();

	ELeafFieldState GetState() const { return State; }

	/** 激活叶子 Field（生成粒子、开始每帧推参）。幂等，重复调用无副作用。 */
	UFUNCTION(BlueprintCallable, Category = "LeafField")
	void ActivateField();

	/** 关闭叶子 Field（停止粒子）。幂等，重复调用无副作用。 */
	UFUNCTION(BlueprintCallable, Category = "LeafField")
	void DeactivateField();

protected:
	// ============================================================
	// 组件
	// ============================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LeafField|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 叶子铺设范围可视化（编辑器黄框） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LeafField|Components")
	TObjectPtr<UBoxComponent> FieldBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LeafField|Components")
	TObjectPtr<UNiagaraComponent> NiagaraComponent;

	/** 朝下正交相机，首次激活时拍一次地面深度 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LeafField|Components")
	TObjectPtr<USceneCaptureComponent2D> HeightCapture;

	// ============================================================
	// 内部状态
	// ============================================================

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> HeightRT = nullptr;

	UPROPERTY(Transient)
	ELeafFieldState State = ELeafFieldState::Dormant;

	/**
	 * HeightRT 是否已拍摄过。地形静态，只需拍一次；若需强制重拍（例如运行时改地形）
	 * 可在蓝图或代码里将此标记重置为 false，下次调用 ActivateField 时会重新 CaptureScene。
	 */
	bool bHeightCaptured = false;

	// ============================================================
	// 内部流程
	// ============================================================

	void PushStaticParams();
	void EnsureHeightCaptured();
	void OnNiagaraCompiled(UNiagaraSystem* InSystem);
	void SyncBoxesToParams();
};
