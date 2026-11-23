// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Bakes /Game/AIGlobe/Textures/T_GlobeLandMask: an equirectangular (2:1) land/ocean mask of the Earth mesh.
 *
 * The Fab earth mesh's UVs are an auto-unwrapped atlas over three textures, so its textures cannot be
 * sampled by direction directly. The baker projects each triangle into longitude/latitude around the
 * mesh's bounds centre (its pivot is offset), samples that triangle's own base colour texture through its
 * UVs, classifies land vs ocean, and writes a grayscale mask (1 = land). The frame is the mesh's local
 * space: lon = atan2(Y, X), lat = asin(Z), U = lon / 2pi + 0.5, V = 0.5 - lat / pi.
 *
 * Also writes Saved/AIGlobe/EarthEquirect.png and LandMask.png previews for checking.
 */
class VFXEDITOR_API FGlobeLandMaskBaker
{
public:
	/** MeshName: asset name of the earth static mesh (looked up in the asset registry). */
	static bool Bake(const FString& MeshName = TEXT("earth_globe"), int32 Width = 2048);

	/** Bakes /Game/AIGlobe/Textures/T_GlobeCityMask the same way: land without ice and snow (where the night-side
	 *  city lights may appear). Leaves T_GlobeLandMask untouched. Preview: Saved/AIGlobe/CityMask.png. */
	static bool BakeCityMask(const FString& MeshName = TEXT("earth_globe"), int32 Width = 2048);
};
