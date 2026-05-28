// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeafField/LeafInteractionSourceComponent.h"
#include "LeafField/LeafInteractionFieldSubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

ULeafInteractionSourceComponent::ULeafInteractionSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

FVector ULeafInteractionSourceComponent::GetSourceWorldLocation() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorLocation();
	}
	return FVector::ZeroVector;
}

void ULeafInteractionSourceComponent::BeginPlay()
{
	Super::BeginPlay();

	PrevLocation = GetSourceWorldLocation();
	bHasPrevLocation = true;

	if (UWorld* World = GetWorld())
	{
		if (ULeafInteractionFieldSubsystem* Sub = World->GetSubsystem<ULeafInteractionFieldSubsystem>())
		{
			Sub->RegisterSource(this);
		}
	}
}

void ULeafInteractionSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (ULeafInteractionFieldSubsystem* Sub = World->GetSubsystem<ULeafInteractionFieldSubsystem>())
		{
			Sub->UnregisterSource(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ULeafInteractionSourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FVector Now = GetSourceWorldLocation();

	// 1) 算这一帧的瞬时速度（位置差分）
	FVector2D InstantVel = FVector2D::ZeroVector;
	if (bHasPrevLocation && DeltaTime > KINDA_SMALL_NUMBER)
	{
		const FVector Delta = (Now - PrevLocation) / DeltaTime; // cm/s
		InstantVel = FVector2D(Delta.X, Delta.Y) * VelocityStrength;
	}
	PrevLocation = Now;
	bHasPrevLocation = true;

	// 2) 不要衰减 → 走老行为，直接吐瞬时速度
	if (VelocityDecayTime <= KINDA_SMALL_NUMBER)
	{
		CachedVelocityXY = InstantVel;
		return;
	}

	// 3) 指数衰减：每帧 K = exp(-dt/Tau)，与帧率无关
	const float K = FMath::Exp(-DeltaTime / VelocityDecayTime);
	CachedVelocityXY *= K;

	if (bUsePeakHold)
	{
		// 峰值保持：瞬时速度比衰减后的旧值大就立刻刷新
		// 效果：起步零延迟、停步柔和衰减
		if (InstantVel.SizeSquared() > CachedVelocityXY.SizeSquared())
		{
			CachedVelocityXY = InstantVel;
		}
	}
	else
	{
		// 纯低通：每帧往瞬时速度靠拢
		const float A = 1.f - K;
		CachedVelocityXY = FMath::Lerp(CachedVelocityXY, InstantVel, A);
	}
}
