// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Creates the editable UI Widget Blueprints under /Game/AIGlobe/UI. An existing Widget Blueprint holds the user's
 * edits, so it is only rebuilt when bOverwrite is set.
 */
class FGlobeWidgetGenerator
{
public:
	/** WBP_GlobeVoiceVisualizer: canvas root with the Globe Voice Visualizer docked at the bottom centre. */
	static bool GenerateVoiceVisualizerWidget(bool bOverwrite = false);

	/** WBP_GlobeStatCard: the glass data card spawned next to each data link. */
	static bool GenerateStatCardWidget(bool bOverwrite = false);

	static bool GenerateAll(bool bOverwrite = false);
};
