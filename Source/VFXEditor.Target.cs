// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

using UnrealBuildTool;
using System.Collections.Generic;

public class VFXEditorTarget : TargetRules
{
	public VFXEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.AddRange(new string[] { "VFX", "VFXEditor" });
	}
}
