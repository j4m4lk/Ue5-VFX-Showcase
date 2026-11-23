// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Builds the Phase 1 hologram material assets. Idempotent:
 *  - M_GlobeHologram: created if missing; otherwise its graph and settings are rebuilt in place
 *    (same asset/GUID, so existing references stay valid). Manual graph edits are overwritten.
 *  - MI_GlobeHologram: created if missing; otherwise only its parent is re-pointed.
 *    Parameter overrides already set on the instance are preserved.
 */
class VFXEDITOR_API FGlobeMaterialGenerator
{
public:
	/** Returns true if both assets exist and saved successfully. */
	static bool Generate();

	/** Only M_GlobePrismSheet / M_GlobePrismSprite and their instances (also part of Generate). */
	static bool GeneratePrismMaterials();

	/** Only M_GlobeCityLights / MI_GlobeCityLights (also part of Generate). Uses T_GlobeLandMask. */
	static bool GenerateCityLightsMaterial();

	/** Only M_GlobeHologram / MI_GlobeHologram (also part of Generate): the shell with its continents layer. */
	static bool GenerateHologramMaterial();

	/** Only M_GlobeLink / MI_GlobeLink (the data-link sprites). */
	static bool GenerateLinkMaterial();
};
