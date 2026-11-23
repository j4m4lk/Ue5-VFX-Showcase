// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "GlobeCloudRingComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * The cloud ring's thin luminous waveform core: one continuous open band (a short cylinder wall) around the
 * component's Z axis. The waveform, vapour and energy response are all in M_GlobeCloudCore; this component only
 * builds the band once (when radius/height/quality change) and forwards a handful of material parameters.
 * UV.x runs 0..1 around the ring (seamless), UV.y 0..1 across the band; normals point radially outward.
 */
UCLASS(ClassGroup = (AIGlobe), meta = (BlueprintSpawnableComponent))
class VFX_API UGlobeCloudRingComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UGlobeCloudRingComponent(const FObjectInitializer& ObjectInitializer);

	/** Rebuilds the band if any input changed. Radius and Height in cm (component space). */
	void Rebuild(float Radius, float Height, int32 Segments);

	/** Per-frame look: Intensity (mode blend x enable), Energy 0..1, peak height (fraction of band), colour. */
	void SetLook(float Intensity, float Energy, float WaveHeight, const FLinearColor& Color);

	/** Any other scalar on M_GlobeCloudCore (spin, centre clearing). */
	void SetScalar(FName Parameter, float Value);

	/** Parent for the runtime material instance (MI_GlobeCloudCore). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Cloud Ring")
	TObjectPtr<UMaterialInterface> CoreMaterial;

private:
	void EnsureMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CoreMID;

	float BuiltRadius = -1.0f;
	float BuiltHeight = -1.0f;
	int32 BuiltSegments = -1;
};
