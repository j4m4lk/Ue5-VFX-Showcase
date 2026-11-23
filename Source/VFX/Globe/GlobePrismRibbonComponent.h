// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "GlobePrismRibbonComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * The prisms ribbon: a flat ring (annulus) in the component's XY plane, repeated once per sheet. The mesh is
 * static; M_GlobePrismSheet makes every sheet undulate (World Position Offset along local Z, same wave formula as the
 * sparkles), shades it like the PS3 XMB ribbon and hides it in front of the globe. Rebuilt only when its size,
 * sheet count or quality change.
 * UV.x = angle 0..1 around the ring (seamless), UV.y = 0 inner edge..1 outer edge.
 * Vertex colour: R = sheet index / (sheets - 1), G = the sheet's wave seed, B = echo k / (echoes + 1) (0 = the sheet
 * itself; echoes are trail copies the material draws behind in time and fainter).
 */
UCLASS(ClassGroup = (AIGlobe), meta = (BlueprintSpawnableComponent))
class VFX_API UGlobePrismRibbonComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UGlobePrismRibbonComponent(const FObjectInitializer& ObjectInitializer);

	/** Rebuilds the sheets if any input changed. Radii in cm (component space). */
	/** Echoes: fading copies of each sheet that run behind in time (the soft trail; 0 = none). */
	void Rebuild(float InnerRadius, float OuterRadius, int32 Layers, int32 Segments, int32 Echoes = 0);

	/** Extra room above and below the flat mesh for the waves (cm), so the ribbon is never culled early. */
	void SetWaveBounds(float HalfHeight);

	void SetScalar(FName Parameter, float Value);
	void SetVector(FName Parameter, const FLinearColor& Value);

	/** The sheet's wave seed (same formula the sparkles use: frac(0.37 + layer * 0.618)). */
	static float LayerSeed(int32 Layer) { return FMath::Frac(0.37f + Layer * 0.618034f); }

	/** Parent for the runtime material instance (MI_GlobePrismSheet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Prism Ribbon")
	TObjectPtr<UMaterialInterface> SheetMaterial;

protected:
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	void EnsureMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SheetMID;

	float BuiltInner = -1.0f;
	float BuiltOuter = -1.0f;
	int32 BuiltLayers = -1;
	int32 BuiltSegments = -1;
	int32 BuiltEchoes = -1;
	float WaveHalfHeight = 0.0f;
};
