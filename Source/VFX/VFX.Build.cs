// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

using UnrealBuildTool;

public class VFX : ModuleRules
{
	public VFX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// Phase 2: surface particles (Niagara) and the outer network mesh.
		PublicDependencyModuleNames.Add("ProceduralMeshComponent"); // UGlobeNetworkComponent derives from it (public header).
		PrivateDependencyModuleNames.Add("Niagara");

		// Microphone input for the voice visualizer (Audio::FAudioCapture; the AudioCapture plugin provides the device backend).
		PrivateDependencyModuleNames.AddRange(new string[] { "AudioCaptureCore", "AudioCapture" });

		// Four-link data display: stat cards are UMG widgets (UGlobeStatCardWidget derives from UUserWidget).
		PublicDependencyModuleNames.Add("UMG");
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "ImageCore" });

		// Lets headers be included as "Globe/..." from this module and from VFXEditor.
		PublicIncludePaths.Add(ModuleDirectory);

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
