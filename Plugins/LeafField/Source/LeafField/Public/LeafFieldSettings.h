#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LeafFieldSettings.generated.h"

// 项目设置 → 插件 → Leaf Field
// 改动后需重启 PIE 生效
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Leaf Field"))
class LEAFFIELD_API ULeafFieldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// 速度场覆盖边长（cm）。角色超出此范围不会扰动粒子
	UPROPERTY(EditAnywhere, config, Category = "VelocityField",
		meta = (ClampMin = "100.0", ClampMax = "2000.0", ForceUnits = "cm"))
	float VelocityFieldWidth = 1000.f;

	// 速度场 RT 分辨率（px）
	UPROPERTY(EditAnywhere, config, Category = "VelocityField",
		meta = (ClampMin = "64", ClampMax = "512"))
	int32 VelocityFieldRTSize = 256;

	// RG8 编码基准（cm/s）。决定速度场能表达的最大速度，通常保持默认值 1000，若角色移速远超 1000 cm/s 可适当调大
	UPROPERTY(EditAnywhere, config, Category = "Wind",
		meta = (ClampMin = "100.0", ClampMax = "5000.0", ForceUnits = "cm/s"))
	float WindMaxSpeed = 1000.f;
};
