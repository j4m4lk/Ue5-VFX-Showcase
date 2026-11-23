// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

using UnrealBuildTool;

// Editor-only module: material graph construction and asset generation.
// Never included in packaged (Game) targets.
public class VFXEditor : ModuleRules
{
	public VFXEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"MaterialEditor",
			"AssetRegistry",
			"RHI",
			"ImageCore",
			"MeshDescription",
			"StaticMeshDescription",
			"Niagara",
			"NiagaraCore",
			"NiagaraEditor",
			"UMG",
			"UMGEditor",
			"Slate",
			"SlateCore",
			"VFX"
		});
	}
}
