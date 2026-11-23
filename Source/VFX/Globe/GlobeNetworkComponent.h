// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "GlobeNetworkComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Optional outer "network" layer: a sparse set of glowing nodes floating outside the globe, joined by a
 * modest number of stable arcs. Built once as a single procedural mesh (one draw call, no per-frame CPU
 * work); pulses travelling along the links are animated in M_GlobeNetwork.
 *
 * Why not Niagara ribbons: ribbons connect particles in spawn order, so stable intentional links between
 * chosen node pairs would need one ribbon ID per link plus persistent particles. A deterministic mesh is
 * simpler, cheaper and exactly repeatable.
 *
 * Geometry lives in component space around its origin; the owner places the component at the sphere centre.
 */
UCLASS(ClassGroup = (AIGlobe), meta = (BlueprintSpawnableComponent))
class VFX_API UGlobeNetworkComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UGlobeNetworkComponent(const FObjectInitializer& ObjectInitializer);

	/** Regenerates the mesh. SurfaceRadius = the points' shell (sizes scale with it); GlobeRadius = the globe's own
	 *  surface, the base for centimetre heights. Cheap; called when radius/settings change. */
	void Rebuild(float SurfaceRadius, float GlobeRadius);

	/** Final brightness (mode blend x energy), written to the material instance. */
	void SetNetworkIntensity(float Intensity);

	/** Centre clearing (see AGlobeVFXActor::CenterClearAmount), written to the material instance. */
	void SetCenterClear(float Amount, float Threshold, float Softness);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "2", ClampMax = "256"))
	int32 NodeCount = 28;

	/** Each node links to this many nearest neighbours (duplicates removed), giving a sparse, stable web. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "1", ClampMax = "6"))
	int32 LinksPerNode = 2;

	/** On: node heights are set in centimetres above the globe's surface (Min/Max Height below).
	 *  Off: as multiples of the points' shell radius (Min/Max Altitude). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network")
	bool bUseCentimetreHeights = true;

	/** Lowest / highest node height above the globe's surface, in cm. Each node gets a random height in between. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (EditCondition = "bUseCentimetreHeights", ClampMin = "0.0", UIMax = "5000.0", Units = "cm"))
	float MinHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (EditCondition = "bUseCentimetreHeights", ClampMin = "0.0", UIMax = "5000.0", Units = "cm"))
	float MaxHeight = 375.0f;

	/** Node altitude range as a multiple of the points' shell radius (used when centimetre heights are off). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (EditCondition = "!bUseCentimetreHeights", ClampMin = "1.0"))
	float MinAltitude = 1.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (EditCondition = "!bUseCentimetreHeights", ClampMin = "1.0"))
	float MaxAltitude = 1.3f;

	/** Extra outward bulge of each arc at its midpoint, as a fraction of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float ArcLift = 0.06f;

	/** Link width and node size as fractions of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "0.0001", ClampMax = "0.05"))
	float LinkWidth = 0.0035f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float NodeSize = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "2", ClampMax = "64"))
	int32 SegmentsPerLink = 16;

	/** Arc lift varies per link by +/- this fraction of ArcLift. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArcLiftRandomness = 0.6f;

	/** Chance per node of one extra link to a random, non-neighbouring node (long arcs across the globe). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RandomLinkChance = 0.25f;

	/** Layout seed used in the editor (and in game when bRandomizeOnPlay is off). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network")
	int32 Seed = 1337;

	/** Pick a fresh random layout every time Play starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network")
	bool bRandomizeOnPlay = true;

	/** In game, links keep drawing themselves from node to node (extend, hold, fade, repeat). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network|Growth")
	bool bAnimateGrowth = true;

	/** Average seconds per extend-hold-fade cycle; each link runs 0.6x-1.4x this with its own phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network|Growth", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "s"))
	float GrowCycleSeconds = 6.0f;

	/** Parent for the runtime material instance (MI_GlobeNetwork). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Network")
	TObjectPtr<UMaterialInterface> NetworkMaterial;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> NetworkMID;

	float BuiltRadius = -1.0f;
	float BuiltGlobeRadius = -1.0f;
	uint32 BuiltSettingsHash = 0;
	/** Chosen once per play session when bRandomizeOnPlay is set. */
	int32 RuntimeSeed = 0;

	int32 EffectiveSeed();
	uint32 SettingsHash(int32 LayoutSeed) const;
	void PushGrowthParameters();
	void EnsureMaterial();
};
