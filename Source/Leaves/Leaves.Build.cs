// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Leaves : ModuleRules
{
	public Leaves(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"Niagara",
			"RenderCore",
			"LeafField"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Leaves",
			"Leaves/Variant_Platforming",
			"Leaves/Variant_Platforming/Animation",
			"Leaves/Variant_Combat",
			"Leaves/Variant_Combat/AI",
			"Leaves/Variant_Combat/Animation",
			"Leaves/Variant_Combat/Gameplay",
			"Leaves/Variant_Combat/Interfaces",
			"Leaves/Variant_Combat/UI",
			"Leaves/Variant_SideScrolling",
			"Leaves/Variant_SideScrolling/AI",
			"Leaves/Variant_SideScrolling/Gameplay",
			"Leaves/Variant_SideScrolling/Interfaces",
			"Leaves/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
