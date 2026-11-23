// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

using UnrealBuildTool;
using System.Collections.Generic;

public class VFXTarget : TargetRules
{
	public VFXTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("VFX");
	}
}
