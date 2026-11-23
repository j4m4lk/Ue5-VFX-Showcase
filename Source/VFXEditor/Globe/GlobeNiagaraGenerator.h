// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Builds /Game/AIGlobe/Niagara/NS_GlobeSurface from Epic's stock Niagara modules (no custom HLSL):
 *   Emitter Update : EmitterState, SpawnRate(User.SpawnRate)
 *   Particle Spawn : InitializeParticle(User.ParticleLifetime, User.GlowColor, User.SpriteSize),
 *                    ShapeLocation(sphere surface, User.SurfaceRadius)
 *   Particle Update: ParticleState, VortexVelocity(User.FlowSpeed), SolveForcesAndVelocity,
 *                    DynamicMaterialParameters(Index 0 = ParticleIntensity, GlobalEnergy, BackSideVisibility, LandMaskStrength)
 *   Renderer       : Sprite, MI_GlobeParticle
 * CPU sim, local space, continuous spawn (steady coverage, no burst gaps). Idempotent: an existing asset is
 * rebuilt in place so actor references stay valid.
 */
class VFXEDITOR_API FGlobeNiagaraGenerator
{
public:
	static bool Generate();

	/** Only NS_GlobePrisms (also part of Generate). Needs MI_GlobePrismSprite. */
	static bool GeneratePrisms();

	/** Only NS_GlobeDataLink (needs M_GlobeLink). */
	static bool GenerateDataLink();
};
