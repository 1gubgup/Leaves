// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeafInteractionSourceComponent.h"
#include "LeafFieldSubsystem.h"
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
		if (ULeafFieldSubsystem* Sub = World->GetSubsystem<ULeafFieldSubsystem>())
		{
			Sub->RegisterSource(this);
		}
	}
}

void ULeafInteractionSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (ULeafFieldSubsystem* Sub = World->GetSubsystem<ULeafFieldSubsystem>())
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

	// 1) 位置差分得到瞬时速度
	FVector2D InstantVel = FVector2D::ZeroVector;
	if (bHasPrevLocation && DeltaTime > KINDA_SMALL_NUMBER)
	{
		const FVector Delta = (Now - PrevLocation) / DeltaTime; // cm/s
		InstantVel = FVector2D(Delta.X, Delta.Y) * VelocityStrength;
	}
	PrevLocation = Now;
	bHasPrevLocation = true;

	// 2) 不衰减：直接吐瞬时速度
	if (VelocityDecayTime <= KINDA_SMALL_NUMBER)
	{
		CachedVelocityXY = InstantVel;
		return;
	}

	// 3) 指数衰减，与帧率无关
	const float K = FMath::Exp(-DeltaTime / VelocityDecayTime);

	if (bUsePeakHold)
	{
		// 峰值保持：起步零延迟，停步柔和衰减
		CachedVelocityXY *= K;
		if (InstantVel.SizeSquared() > CachedVelocityXY.SizeSquared())
		{
			CachedVelocityXY = InstantVel;
		}
	}
	else
	{
		// 纯低通：output = K*prev + (1-K)*input，只乘一次 K
		CachedVelocityXY = CachedVelocityXY * K + InstantVel * (1.f - K);
	}
}
