// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Globe/GlobeDataTypes.h"
#include "GlobeVFXActor.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UGlobeNetworkComponent;
class UGlobeCloudRingComponent;
class UGlobeDataLinksComponent;
class UGlobePrismRibbonComponent;

/** Test source for the cloud ring's audio energy (no microphone / TTS in this phase). */
UENUM(BlueprintType)
enum class EGlobeTestAudio : uint8
{
	Off UMETA(DisplayName = "Off (use SetAudioEnergy)"),
	Level0 UMETA(DisplayName = "0"),
	Level25 UMETA(DisplayName = "0.25"),
	Level50 UMETA(DisplayName = "0.5"),
	Level100 UMETA(DisplayName = "1"),
	SpeechPattern UMETA(DisplayName = "Simulated Speech"),
};

UENUM(BlueprintType)
enum class EGlobeRingQuality : uint8
{
	Low,
	Medium,
	High,
};

/**
 * Reusable holographic globe rig.
 *  - Phase 1: a translucent Fresnel-rim sphere whose surface animation lives in the material.
 *  - Phase 2: a persistent Niagara point shell (NS_GlobeSurface) and an optional network mesh, both centred on
 *    the fitted sphere and attached to this actor, so they follow its location, rotation and scale.
 * Tick runs only during FadeTo. The particle layers never restart on swaps: the viewer pawn calls
 * SetEarthModeBlend during its crossfade and the actor pushes system-level Niagara User parameters.
 */
UCLASS(Blueprintable, ClassGroup = (AIGlobe), meta = (DisplayName = "Globe VFX Actor"))
class VFX_API AGlobeVFXActor : public AActor
{
	GENERATED_BODY()

public:
	AGlobeVFXActor();

	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void SetGlobeMaterial(UMaterialInterface* NewMaterial);

	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void SetGlobeColor(FLinearColor NewColor);

	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void SetGlowIntensity(float NewGlowIntensity);

	/** 0 = invisible, 1 = default transparency, 2 = more opaque. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void SetHologramOpacity(float NewOpacity);

	/** Clamped to [0, 1]. 0 is the idle baseline, 1 is fully energised. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void SetEnergy(float NewEnergy);

	UFUNCTION(BlueprintPure, Category = "Globe VFX")
	float GetEnergy() const { return Energy; }

	/** Animates the material Fade parameter (1 = visible, 0 = gone) over Duration seconds. Ticks only while running. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void FadeTo(float TargetFade, float Duration);

	/** Sets Fade instantly and cancels any running fade. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void SetFade(float NewFade);

	UFUNCTION(BlueprintPure, Category = "Globe VFX")
	float GetFade() const { return CurrentFade; }

	UFUNCTION(BlueprintPure, Category = "Globe VFX")
	bool IsFading() const { return FadeDuration > 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Globe VFX")
	UStaticMeshComponent* GetGlobeMesh() const { return GlobeMesh; }

	/** 0 = hologram mode, 1 = realistic Earth mode. Interpolates particle/network intensity and back-side visibility. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Particles")
	void SetEarthModeBlend(float Blend);

	/** Re-fits the particle sphere (centre, radius, rotation) to SurfaceFitActor or the hologram and re-pushes parameters. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Particles")
	void RefreshSurface();

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Particles")
	UNiagaraComponent* GetSurfaceParticles() const { return SurfaceParticles; }

	/** Audio energy input for the cloud ring, 0..1 (future microphone/TTS hook). Smoothed with Ring Attack /
	 *  Ring Release. Ignored while Test Audio is not Off. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Cloud Ring")
	void SetAudioEnergy(float NewAudioEnergy);

	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Cloud Ring")
	void SetTestAudio(EGlobeTestAudio Mode);

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Cloud Ring")
	UNiagaraComponent* GetCloudRingMist() const { return CloudRingMist; }

	/** Turns the cloud mist around the ring on or off (the ring's core is not affected). */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Cloud Ring")
	void SetCloudMistEnabled(bool bEnabled);

	/** Cloth look for the prism ribbons (flutter, soft trail, faster). Off = the original smooth ribbons. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Prisms")
	void SetClothRibbonsEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Prisms")
	bool IsClothRibbonsEnabled() const { return bClothRibbons; }

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Cloud Ring")
	bool IsCloudMistEnabled() const { return bShowCloudMist; }

	/** Turns the ring's thin glowing line on or off (the mist is not affected). */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Cloud Ring")
	void SetRingLineEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Cloud Ring")
	bool IsRingLineEnabled() const { return bShowRingCore; }

	/** The ring's current drive: saturate(smoothed audio + Energy * GlobalEnergyRingContribution). */
	UFUNCTION(BlueprintPure, Category = "Globe VFX|Cloud Ring")
	float GetRingEnergy() const { return RingEnergy; }

	/** Editor button: moves SurfaceFitActor (the earth) so its true sphere centre sits at the hologram's centre,
	 *  so the earth never pokes through the hologram shell. Scale and rotation are untouched. */
	UFUNCTION(CallInEditor, Category = "Globe VFX|Earth Overlay")
	void CenterEarthInHologram();

	/** Centre of the fitted globe sphere (the earth or the hologram), in world space. */
	UFUNCTION(BlueprintPure, Category = "Globe VFX")
	FVector GetGlobeCenter() const;

	/** Radius of the fitted globe surface (cm, world scale), without the points' height. */
	UFUNCTION(BlueprintPure, Category = "Globe VFX")
	float GetGlobeSurfaceRadius() const;

	/** The four-link data display: call SetDataItems / UpdateDataItem / AddDataItem / RemoveDataItem on it. */
	UFUNCTION(BlueprintPure, Category = "Globe VFX|Data Links")
	UGlobeDataLinksComponent* GetDataLinks() const { return DataLinks; }

	/** May be null if no material is assigned. */
	UFUNCTION(BlueprintPure, Category = "Globe VFX")
	UMaterialInstanceDynamic* GetDynamicMaterial() const { return DynamicMaterial; }

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Prisms")
	UNiagaraComponent* GetPrisms() const { return Prisms; }

	/** Switches the prisms on or off (the viewer pawn's R key). On: the ribbon (band width, wave height) and sparkles
	 *  grow from Prism Ramp Min Scale to full size over Prism Ramp Duration; off: they shrink back, fade, and stop.
	 *  Pressing again mid-ramp reverses smoothly. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Prisms")
	void SetPrismsOn(bool bOn);

	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Prisms")
	void TogglePrisms() { SetPrismsOn(!bEnablePrisms); }

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Prisms")
	bool ArePrismsOn() const { return bEnablePrisms; }

	UFUNCTION(BlueprintPure, Category = "Globe VFX|Rotation")
	bool IsGlobeRotationLocked() const { return bLockGlobeRotation; }

	/** Re-reads the sun direction for the city lights (Tick does this while playing; Globe Time Of Day calls it in the
	 *  editor when its slider moves). */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|NightLights")
	void UpdateCityLightsSun();

	/** Turns the globe back to the orientation it had when play started (locked layers are unaffected). */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Rotation")
	void ResetGlobeRotation();

	/** Live shape tweak: full circle or behind-only, tilt (degrees) and its direction, size and intensity. The waves
	 *  keep running (nothing restarts). */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Prisms")
	void SetPrismShape(bool bFullCircle, float Tilt, float TiltDirection, float Size, float Intensity);

	/** Turns each layer on or off independently (e.g. waves only, to compare them by themselves). The waves keep
	 *  their paths when the other layers change; turning the waves themselves back on restarts them with a fade-in. */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX")
	void SetLayersEnabled(bool bSurfacePoints, bool bNetworkLayer, bool bCloudRingLayer, bool bPrismLayer);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	/** Applies Details-panel edits made while playing (OnConstruction does not rerun in a game world). */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX")
	TObjectPtr<UStaticMeshComponent> GlobeMesh;

	/** Parent for the dynamic instance: MI_GlobeHologram or any child of M_GlobeHologram. Auto-filled with MI_GlobeHologram when placed in the editor while empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Material")
	TObjectPtr<UMaterialInterface> GlobeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Look", meta = (HideAlphaChannel))
	FLinearColor GlobeColor = FLinearColor(0.0f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Look", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float GlowIntensity = 3.0f;

	/** Hologram transparency: 0 = invisible, 1 = default, up to 2 = more opaque. Finer control: RimOpacity /
	 *  CoreOpacity on MI_GlobeHologram (edge vs. centre opacity). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Look", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HologramOpacity = 1.0f;

	/** While the earth is shown inside the kept hologram, how clear the hologram's centre becomes
	 *  (0 = unchanged, 1 = fully clear). The rim keeps glowing. Ramps with the swap crossfade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Earth Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HologramCenterClear = 1.0f;

	/** Editor only: preview the earth-mode look (cleared hologram centre) in the level viewport while tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Earth Overlay")
	bool bPreviewEarthModeInEditor = false;

	/** Size of the cleared hologram centre as a fraction of the globe's on-screen radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Earth Overlay", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float HologramCenterClearSize = 0.6f;

	/** Width of the soft edge between the clear centre and the glowing rim. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Earth Overlay", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float HologramCenterClearSoftness = 0.15f;

	/** Initial energy. Changed at runtime via SetEnergy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Energy = 0.2f;

	// ---------------- Rotation ----------------
	// The globe stays at the centre; the viewer orbits / zooms the camera around it and right-drags to turn it.
	// Locked layers (voice ring, ribbons) keep the orientation set here, whatever the globe does.

	/** Stop the viewer's right-drag from rotating the globe (the camera can still orbit and zoom around it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Lock Globe Rotation"))
	bool bLockGlobeRotation = false;

	/** Globe spin around its own axis while playing (degrees per second; negative reverses; 0 = still). Turns the
	 *  hologram, earth, surface points and network, plus any layer whose lock is off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Globe Spin", UIMin = "-90.0", UIMax = "90.0", Units = "deg"))
	float GlobeSpinSpeed = 0.0f;

	// ---------------- Phase 2: surface particles ----------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles")
	TObjectPtr<UNiagaraComponent> SurfaceParticles;

	/** Separate toggle for the surface point shell (NS_GlobeSurface). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles")
	bool bEnableSurfaceParticles = true;

	/** NS_GlobeSurface. Auto-filled when placed in the editor while empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles")
	TObjectPtr<UNiagaraSystem> SurfaceSystem;

	/** Mesh actor whose true sphere (bounds centre, radius, rotation) the points hug, e.g. the Fab earth.
	 *  Its pivot need not be centred. Empty = fit to the hologram sphere. Attach it to this actor so both move together. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Globe VFX|Particles")
	TObjectPtr<AActor> SurfaceFitActor;

	/** On: points spawn exactly on the GlobeVFX (hologram) sphere's outer surface, which sits just outside the
	 *  earth mesh. Points Height / Radius Scale / Radius Override are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Points Position", meta = (DisplayName = "Spawn On Hologram Surface"))
	bool bSpawnOnHologramSurface = false;

	/** > 0 replaces the fitted radius (cm, before scale and height). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Points Position", meta = (ClampMin = "0.0", Units = "cm"))
	float SurfaceRadiusOverride = 0.0f;

	/** Height of the point shell above the fitted surface (cm). All points sit exactly at this height and stay there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Points Position", meta = (DisplayName = "Points Height", UIMin = "-50.0", UIMax = "500.0", Units = "cm"))
	float SurfaceOffset = 12.0f;

	/** Multiplies the fitted radius before Points Height is added (1 = on the surface, 1.05 = 5% further out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Points Position", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float PointsRadiusScale = 1.0f;

	/** Average number of visible points (desktop quality). Scaled by effects quality when enabled below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Density", meta = (ClampMin = "0", ClampMax = "20000", UIMax = "5000"))
	int32 ParticleCount = 1200;

	/** Multiply ParticleCount by the Effects scalability level (Low 0.35, Medium 0.6, High 0.85, Epic/Cinematic 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Density")
	bool bScaleWithEffectsQuality = true;

	/** Seconds each point lives (fades in, twinkles, fades out). Spawn rate = count / lifetime keeps coverage steady. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Density", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "s"))
	float ParticleLifetime = 4.0f;

	/** Sprite size as a fraction of the radius (0.011 = ~12 cm on an 11 m globe). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Look", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float SpriteSizeFactor = 0.011f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Look", meta = (HideAlphaChannel))
	FLinearColor ParticleColor = FLinearColor(0.15f, 0.75f, 1.0f, 1.0f);

	/** Flow around the globe axis in degrees per second at Energy 0 (idle, never fully still) and Energy 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Look", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float IdleFlowDegreesPerSecond = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Look", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float EnergizedFlowDegreesPerSecond = 5.0f;

	/** Off (default): the swap only turns the globe models on/off; particles and network look the same in both
	 *  modes (VFX-mode values). On: they blend to the Earth-mode values below during the swap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes")
	bool bChangeParticlesOnSwap = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float VFXModeParticleIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes", meta = (EditCondition = "bChangeParticlesOnSwap", ClampMin = "0.0", UIMax = "4.0"))
	float EarthModeParticleIntensity = 1.0f;

	/** On: while the earth is shown, points are drawn on top of it (no depth test), so the solid earth never hides
	 *  them whatever their exact height. Far-side points are removed by the camera-facing test instead, so they
	 *  never draw through the earth. Hologram mode keeps the normal depth-tested points. Off: always depth-tested. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes", meta = (DisplayName = "Draw Points On Top"))
	bool bDrawPointsOnTop = true;

	/** Sprite materials for the two modes (MI_GlobeParticle / MI_GlobeParticleOnTop). Auto-filled in the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes")
	TObjectPtr<UMaterialInterface> PointsMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes")
	TObjectPtr<UMaterialInterface> PointsOnTopMaterial;

	/** On: only points on the camera-facing side of the globe are drawn; far-side points are not rendered at all
	 *  (they reappear as you orbit round to them). Overrides the back-side visibility values below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes", meta = (DisplayName = "Front Points Only"))
	bool bFrontPointsOnly = false;

	/** Brightness of far-side points. The translucent hologram does not hide them, so they are dimmed to keep
	 *  the sphere readable; on the opaque earth they are hidden by depth anyway. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VFXModeBackSideVisibility = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Modes", meta = (EditCondition = "bChangeParticlesOnSwap", ClampMin = "0.0", ClampMax = "1.0"))
	float EarthModeBackSideVisibility = 1.0f;

	/** How much particles and network links fade in the middle of the view (0 = off, 1 = fully cleared),
	 *  so the globe's texture reads in the centre while the rim keeps its full particle outline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Center Clear", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CenterClearAmount = 0.7f;

	/** Size of the cleared area as a fraction of the globe's on-screen radius (0.5 = inner half). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Center Clear", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float CenterClearSize = 0.5f;

	/** Width of the soft transition between the cleared centre and the full rim. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Center Clear", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float CenterClearSoftness = 0.12f;

	/** 0 = uniform coverage; 1 = ocean points dimmed to MI_GlobeParticle's OceanBrightness so continents read.
	 *  Aligned only when SurfaceFitActor is the earth mesh T_GlobeLandMask was baked from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Geography", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LandMaskStrength = 0.85f;

	/** On: surface points appear only on the land masses (ocean points are not drawn, and more are spawned so the
	 *  continents stay well covered). Off: points everywhere, oceans dimmed by Land Mask Strength. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Geography", meta = (DisplayName = "Points On Land Only"))
	bool bPointsOnLandOnly = true;

	/** Spawn multiplier while Points On Land Only is on (about a third of the sphere is land). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles|Geography", meta = (EditCondition = "bPointsOnLandOnly", ClampMin = "1.0", ClampMax = "5.0"))
	float LandPointsDensity = 2.5f;

	// ---------------- Hologram continents ----------------

	/** Draw the continents on the hologram shell (same blue), lined up with the real earth's land masses. They fade
	 *  out as the real earth fades in on S, and back in on the way back, so the hologram reads as the earth turning
	 *  on. Needs Surface Fit Actor (the earth) for the alignment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Hologram Continents", meta = (DisplayName = "Show Continents"))
	bool bHologramShowsLand = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Hologram Continents", meta = (DisplayName = "Land Color", HideAlphaChannel))
	FLinearColor HologramLandColor = FLinearColor(0.0f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Hologram Continents", meta = (DisplayName = "Land Brightness", ClampMin = "0.0", UIMax = "10.0"))
	float HologramLandFill = 1.2f;

	/** How solid the land reads on the translucent shell (0 = glow only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Hologram Continents", meta = (DisplayName = "Land Opacity", ClampMin = "0.0", ClampMax = "1.0"))
	float HologramLandOpacity = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Hologram Continents", meta = (DisplayName = "Coastline Glow", ClampMin = "0.0", UIMax = "20.0"))
	float HologramCoastGlow = 3.0f;

	/** Translucent draw order; above the hologram shell (the viewer pawn sets that to 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Particles")
	int32 ParticleSortPriority = 2;

	// ---------------- Phase 2: optional outer network ----------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|Network")
	TObjectPtr<UGlobeNetworkComponent> Network;

	/** Separate toggle / scalability tier for the outer node-and-link layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Network")
	bool bEnableNetwork = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Network", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float VFXModeNetworkIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Network", meta = (EditCondition = "bChangeParticlesOnSwap", ClampMin = "0.0", UIMax = "4.0"))
	float EarthModeNetworkIntensity = 0.6f;

	/** Apply the centre clearing to the network links/nodes too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Network")
	bool bCenterClearNetwork = true;

	// ---------------- Phase 3: cloud wave ring ----------------

	/** Thin luminous waveform core (band mesh + M_GlobeCloudCore). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring")
	TObjectPtr<UGlobeCloudRingComponent> CloudRingCore;

	/** Soft mist layer (NS_GlobeCloudWave). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring")
	TObjectPtr<UNiagaraComponent> CloudRingMist;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring")
	bool bEnableCloudRing = true;

	/** The soft cloud mist around the ring (off keeps only the thin glowing core). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (DisplayName = "Show Cloud Mist", EditCondition = "bEnableCloudRing"))
	bool bShowCloudMist = true;

	/** The ring's thin glowing line (off keeps only the cloud mist). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (DisplayName = "Show Ring Line", EditCondition = "bEnableCloudRing"))
	bool bShowRingCore = true;

	/** NS_GlobeCloudWave. Auto-filled when placed in the editor while empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring")
	TObjectPtr<UNiagaraSystem> CloudWaveSystem;

	/** Ring centre-line radius in cm. 0 = auto: 1.15 x the globe's surface radius (just outside the points). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (ClampMin = "0.0", UIMax = "5000.0", Units = "cm"))
	float RingRadius = 0.0f;

	/** Height of the band (cm). The mist tube is about a third of this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (ClampMin = "5.0", UIMax = "1000.0", Units = "cm"))
	float RingThickness = 240.0f;

	/** On (default): the voice ring keeps the orientation you set (Voice Ring Rotation) when the globe is rotated
	 *  (right-drag, Globe Spin); it still stays centred on the globe and follows its movement and scale. Off: it
	 *  turns with the globe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Lock Voice Ring Rotation"))
	bool bLockVoiceRingRotation = true;

	/** Voice ring spin around its own axis (degrees per second; negative reverses). The waveform
	 *  pattern slides around the band and the mist swirl is matched to the same angular speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Voice Ring Spin", UIMin = "-90.0", UIMax = "90.0", Units = "deg"))
	float RingSpinSpeed = 15.0f;

	/** While the earth is shown, how much the ring fades in the middle of the view (front arc over the globe,
	 *  back arc behind it) so it does not block the globe when zoomed in. 0 = off, 1 = fully clear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring|Earth Center Clear", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RingCenterClear = 1.0f;

	/** Size of the cleared area as a fraction of the globe's on-screen radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring|Earth Center Clear", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float RingCenterClearSize = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring|Earth Center Clear", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float RingCenterClearSoftness = 0.15f;

	/** Orientation of the voice ring (Pitch / Roll tilt it away from the equator, Yaw turns it). Relative to the
	 *  globe's placed orientation when locked, to its current one when not. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Voice Ring Rotation"))
	FRotator RingTilt = FRotator::ZeroRotator;

	/** Peak height of the waveform as a fraction of the band height (at full energy; idle is 35% of this). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float WaveHeight = 0.3f;

	/** Mist particles per second at High quality and idle (rises up to 1.6x with energy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (ClampMin = "0.0", UIMax = "400.0"))
	float CloudDensity = 70.0f;

	/** Mist puff size (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (ClampMin = "1.0", UIMax = "500.0", Units = "cm"))
	float MistSize = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (HideAlphaChannel))
	FLinearColor RingColor = FLinearColor(0.2f, 0.75f, 1.0f, 1.0f);

	/** Ring brightness in hologram mode and in realistic earth mode; blended during the S crossfade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float VFXModeRingIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float EarthModeRingIntensity = 0.85f;

	/** How fast the ring rises toward louder audio / falls back toward quiet (per second; higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring|Audio", meta = (ClampMin = "0.1", UIMax = "30.0"))
	float RingAttackSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring|Audio", meta = (ClampMin = "0.1", UIMax = "30.0"))
	float RingReleaseSpeed = 2.5f;

	/** How much the globe's own Energy adds to the ring on top of audio (ring = saturate(audio + Energy x this)). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlobalEnergyRingContribution = 0.2f;

	/** Test driver for the ring: fixed levels or a repeating simulated speech pattern. Off = SetAudioEnergy input.
	 *  Change it during Play, or use the console: AIGlobe.TestAudio 0|0.25|0.5|1|speech|off */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring|Audio")
	EGlobeTestAudio TestAudio = EGlobeTestAudio::Off;

	/** Mist spawn count and band smoothness (Low 40% / 64 segments, Medium 70% / 128, High 100% / 256). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Cloud Ring")
	EGlobeRingQuality RingQuality = EGlobeRingQuality::High;

	// ---------------- Prisms: PS3-XMB-style ribbon around the globe ----------------

	/** The ribbon: a flat ring of several translucent sheets that undulate and cross each other, glowing where a
	 *  sheet folds or is seen edge-on (the PS3 XMB wave look). The waving runs in M_GlobePrismSheet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms")
	TObjectPtr<UGlobePrismRibbonComponent> PrismRibbon;

	/** Sparkles riding the ribbon's surface (NS_GlobePrisms). Niagara simulates them; this actor only sets
	 *  system-level User parameters (including the shared wave clock). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms")
	TObjectPtr<UNiagaraComponent> Prisms;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms")
	bool bEnablePrisms = true;

	/** NS_GlobePrisms (sparkles). Auto-filled when placed in the editor while empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms")
	TObjectPtr<UNiagaraSystem> PrismsSystem;

	/** Globe radius the ribbon is sized from, in the actor's unscaled units (cm at actor scale 1; the hologram sphere
	 *  is 50). Multiplied by the actor scale exactly once. 0 = auto: the outer of the hologram shell and the fitted
	 *  earth, measured from their scaled mesh bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float PrismGlobeRadius = 0.0f;

	/** Size of the whole ribbon (inner and outer edge, wave height scale with it). Below 1 the inner edge is still kept
	 *  just outside the globe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Shape", meta = (DisplayName = "Size", ClampMin = "0.6", ClampMax = "3.0"))
	float PrismRadiusMultiplier = 1.0f;

	/** Inner and outer edge of the ribbon as multiples of the globe radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Ribbon", meta = (ClampMin = "1.02", ClampMax = "4.0"))
	float RibbonInnerRadius = 1.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Ribbon", meta = (ClampMin = "1.05", ClampMax = "5.0"))
	float RibbonOuterRadius = 1.75f;

	/** Peak height of the waves as a fraction of the globe radius (energy raises it up to 1.3x). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Ribbon", meta = (ClampMin = "0.0", ClampMax = "0.6"))
	float RibbonWaveHeight = 0.16f;

	/** Number of stacked sheets (the XMB ribbon is several crossing sheets). The last one is gold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Ribbon", meta = (ClampMin = "1", ClampMax = "8"))
	int32 RibbonLayers = 5;


	/** Brightness where a sheet folds or is seen edge-on (the bright XMB lines), and of the flat sheet itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Ribbon", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float RibbonEdgeGlow = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Ribbon", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float RibbonFill = 0.035f;

	/** Bright trail lines running along each sheet and drifting across it with the waves. Unlike the fold glow they
	 *  do not depend on the viewing angle, so a tilted ribbon seen face-on still shows bright trails. 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Shape", meta = (DisplayName = "Trail Line Glow", ClampMin = "0.0", UIMax = "10.0"))
	float RibbonLineGlow = 2.0f;

	/** Width of the trail lines as a fraction of the band width (smaller = thinner, crisper threads). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Shape", meta = (DisplayName = "Trail Line Width", ClampMin = "0.001", ClampMax = "0.3"))
	float RibbonLineWidth = 0.008f;

	/** Soft glow around the trail lines (0 = bare threads). Trail Line Glow sets the lines' own brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Shape", meta = (DisplayName = "Trail Line Halo", ClampMin = "0.0", UIMax = "2.0"))
	float RibbonLineHalo = 0.35f;

	/** Wave animation speed (1 = gentle XMB pace; energy speeds it up to 2x). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float PrismSpeed = 1.0f;

	// ---------------- Cloth: flutter and soft trail ----------------

	/** Cloth look: flutter, soft trail and faster motion. Off = the ribbons exactly as before (smooth XMB waves). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Cloth", meta = (DisplayName = "Cloth Ribbons"))
	bool bClothRibbons = true;

	/** Extra wave speed while Cloth Ribbons is on (multiplies Prism Speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Cloth", meta = (DisplayName = "Cloth Speed", ClampMin = "0.1", UIMax = "4.0", EditCondition = "bClothRibbons"))
	float ClothSpeedMultiplier = 1.6f;

	/** Fast small ripples on the sheets, stronger toward the outer edge, so they move like cloth (0 = smooth XMB waves). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Cloth", meta = (DisplayName = "Cloth Flutter", ClampMin = "0.0", UIMax = "1.5", EditCondition = "bClothRibbons"))
	float RibbonFlutter = 0.6f;

	/** How fast the flutter ripples move (1 = brisk; they also quicken a little with energy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Cloth", meta = (DisplayName = "Flutter Speed", ClampMin = "0.0", UIMax = "4.0", EditCondition = "bClothRibbons"))
	float FlutterSpeed = 1.0f;

	/** Faint copies of each sheet that follow a moment behind it: a soft motion trail (0 = off; each adds a sheet copy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Cloth", meta = (DisplayName = "Trail Copies", ClampMin = "0", ClampMax = "8", EditCondition = "bClothRibbons"))
	int32 RibbonTrailEchoes = 3;

	/** Seconds the last trail copy runs behind the sheet (longer = a longer, more smeared trail). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Cloth", meta = (DisplayName = "Trail Length", ClampMin = "0.0", UIMax = "2.0", Units = "s", EditCondition = "bClothRibbons"))
	float RibbonTrailTime = 0.3f;

	/** Brightness of the first trail copy (the others fade from it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Cloth", meta = (DisplayName = "Trail Opacity", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bClothRibbons"))
	float RibbonTrailOpacity = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms", meta = (HideAlphaChannel))
	FLinearColor BlueColor = FLinearColor(0.08f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms", meta = (HideAlphaChannel))
	FLinearColor GoldColor = FLinearColor(1.0f, 0.68f, 0.3f, 1.0f);

	/** Overall brightness. Energy (Globe VFX|Look > Energy, SetEnergy) raises speed, brightness and wave height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Shape", meta = (DisplayName = "Intensity", ClampMin = "0.0", UIMax = "4.0"))
	float PrismIntensity = 1.0f;

	/** On: one connected ring all the way round the globe (it also passes in front of it). Off: only the part behind
	 *  the globe shows, framing it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Shape", meta = (DisplayName = "Full Circle"))
	bool bPrismFullCircle = false;

	/** Tilt of the ring away from the globe's equator (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Ribbon Tilt", UIMin = "-90.0", UIMax = "90.0", Units = "deg"))
	float PrismTilt = 12.0f;

	/** Which way the ring leans (degrees around the globe's axis; 0 = it rises toward +Y / the viewer's right side). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Ribbon Tilt Direction", UIMin = "-180.0", UIMax = "180.0", Units = "deg"))
	float PrismTiltDirection = 0.0f;

	/** With Full Circle off: brightness of the half in front of the globe (0 = hidden, only the back shows). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms", meta = (EditCondition = "!bPrismFullCircle", ClampMin = "0.0", ClampMax = "1.0"))
	float PrismFrontVisibility = 0.0f;

	/** Sparkles on the ribbon (1 = ~60 per second at High quality; 0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Sparkles", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float SparkleAmount = 1.0f;

	/** Sparkle size (cm on an 11.5 m globe; scales with the globe). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Sparkles", meta = (ClampMin = "1.0", UIMax = "60.0", Units = "cm"))
	float SparkleSize = 24.0f;

	/** Mesh segments around the ring and sparkle amount (Low 128 / 40%, Medium 192 / 70%, High 256 / 100%). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms")
	EGlobeRingQuality PrismQuality = EGlobeRingQuality::High;

	/** On (default): the ribbons keep the tilt you set when the globe is rotated; they still follow its position and
	 *  scale. Off: they turn with the globe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Lock Ribbon Rotation"))
	bool bLockPrismRotation = true;

	/** Ribbon spin around its own axis (degrees per second; negative reverses; 0 = only the waves move). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Rotation", meta = (DisplayName = "Ribbon Spin", UIMin = "-90.0", UIMax = "90.0", Units = "deg"))
	float PrismSpinSpeed = 0.0f;

	/** Brightness in hologram mode and realistic earth mode; blended during the S crossfade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Modes", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float VFXModePrismIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Modes", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float EarthModePrismIntensity = 1.0f;

	/** Brightness of ribbon parts covered by the globe. The hologram is translucent, so they are only dimmed; the
	 *  earth hides them (0), also while it is still translucent mid-crossfade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Modes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VFXModePrismBackSide = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|Modes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EarthModePrismBackSide = 0.0f;

	/** Seconds to grow from the min to the max size when switched on with R (and back when switched off). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|On Off", meta = (ClampMin = "0.0", UIMax = "10.0", Units = "s"))
	float PrismRampDuration = 2.5f;

	/** Size the ribbon (band width and wave height) and sparkles start from when switched on (fraction of full size). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Prisms|On Off", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PrismRampMinScale = 0.15f;

	/** World-space globe radius the ribbon is sized from (cm), for reference. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Globe VFX|Prisms", meta = (Units = "cm"))
	float AppliedPrismRadius = 0.0f;

	// ---------------- NightLights (city lights on the night side) ----------------

	/** Night-side city lights: a thin shell just above the earth (M_GlobeCityLights). Lights sit only on land
	 *  (T_GlobeLandMask, baked from the earth's own textures), blink on and off, and show only on the side of the
	 *  earth facing away from the sun (the level's Directional Light, i.e. Globe Time Of Day). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights")
	TObjectPtr<UStaticMeshComponent> CityLights;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights")
	bool bEnableCityLights = true;

	/** MI_GlobeCityLights. Auto-filled when placed in the editor while empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights")
	TObjectPtr<UMaterialInterface> CityLightsMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Brightness", ClampMin = "0.0", UIMax = "20.0"))
	float CityLightsIntensity = 3.0f;

	/** Share of land with lights (0 = none, 1 = crowded). Regions vary around this, like real populated areas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Density", ClampMin = "0.0", ClampMax = "1.0"))
	float CityLightsDensity = 0.35f;

	/** Light size range, as a fraction of a light's grid cell. Most lights sit near Size Min; Big Share of them grow
	 *  toward Size Max (big cities). Lights are never clipped, so large ones stay round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Size Min", ClampMin = "0.01", ClampMax = "0.45"))
	float CityLightsSizeMin = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Size Max", ClampMin = "0.01", ClampMax = "0.45"))
	float CityLightsSizeMax = 0.3f;

	/** Share of lights that are big (near Size Max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Big Share", ClampMin = "0.0", ClampMax = "1.0"))
	float CityLightsBigShare = 0.1f;

	/** Soft glow around each light's sharp core (0 = crisp points). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Halo", ClampMin = "0.0", UIMax = "2.0"))
	float CityLightsHalo = 0.3f;

	/** How fine the grid of light sites is (2 = twice as many, smaller lights). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Fineness", ClampMin = "0.25", ClampMax = "4.0"))
	float CityLightsScale = 1.0f;

	/** Half the lights blink: how much of each cycle they are off (0 = never) and cycles per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Blink Off Time", ClampMin = "0.0", ClampMax = "0.95"))
	float CityLightsBlink = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Blink Speed", ClampMin = "0.0", UIMax = "5.0"))
	float CityLightsBlinkSpeed = 0.35f;

	/** Faint glow over populated land, and the width of the day/night line (bigger = softer dusk). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Urban Glow", ClampMin = "0.0", UIMax = "2.0"))
	float CityLightsGlow = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Terminator Softness", ClampMin = "0.01", ClampMax = "1.0"))
	float CityLightsNightSoftness = 0.12f;

	/** How dark the night side of the earth gets (0 = only the lights, 1 = black like from space). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Night Side Darkness", ClampMin = "0.0", ClampMax = "1.0"))
	float CityNightDarkness = 0.85f;

	/** Each light gets a colour between Color A and Color B (warm sodium orange to white by default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Color A", HideAlphaChannel))
	FLinearColor CityLightsColor = FLinearColor(1.0f, 0.62f, 0.28f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (DisplayName = "Color B", HideAlphaChannel))
	FLinearColor CityLightsColorB = FLinearColor(1.0f, 0.88f, 0.65f, 1.0f);

	/** Shown in realistic earth mode; off in hologram mode by default (the earth is hidden there). Blended with S. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VFXModeCityLights = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EarthModeCityLights = 1.0f;

	/** Off: ignore the sun and light every land area (useful to check the land mask). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|NightLights")
	bool bCityLightsNightSideOnly = true;

	// ---------------- Four-link data display ----------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe VFX|Data Links")
	TObjectPtr<UGlobeDataLinksComponent> DataLinks;

	/** Cards shown when play starts (max 4, in slot order: top-left, top-right, bottom-left, bottom-right).
	 *  Add them with Add Data Item below or the + button; edit Value / Secondary / Progress / Details per card.
	 *  Editing this list while playing updates the cards live. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Data Links", meta = (TitleProperty = "Title"))
	TArray<FGlobeDataItem> DataItems;

	/** Name for the next card created with Add Data Item (empty = "Item N"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe VFX|Data Links", meta = (DisplayName = "New Item Name"))
	FText NewDataItemName;

public:
	/** Adds a card named New Item Name to Data Items (works in the editor and while playing). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Globe VFX|Data Links", meta = (DisplayName = "Add Data Item"))
	void AddDataItemFromName();

	/** Removes the last card from Data Items. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Globe VFX|Data Links", meta = (DisplayName = "Remove Last Data Item"))
	void RemoveLastDataItem();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Globe VFX|Data Links", meta = (DisplayName = "Clear Data Items"))
	void ClearAllDataItems();

	/** Pushes Data Items to the display (called at BeginPlay and after edits while playing). */
	UFUNCTION(BlueprintCallable, Category = "Globe VFX|Data Links")
	void ApplyDataItems();

protected:

private:
	/** Creates the DMI if missing or if its parent no longer matches GlobeMaterial. Returns null without a material. */
	UMaterialInstanceDynamic* EnsureDynamicMaterial();

	void PushAllParameters();

	/** Fills empty Ids from the Title and makes all Ids unique. */
	void FixDataItemIds();

	/** Pushes CurrentFade to the material and (in game) mesh visibility. Never changes scale. */
	void ApplyFade();

	/** World-space sphere the points hug: centre, radius (incl. offset) and orientation. */
	void ComputeSurfaceSphere(FVector& OutCenter, float& OutRadius, FQuat& OutRotation) const;

	/** Pushes Energy- and mode-dependent values (cheap; called on Energy and blend changes). */
	void PushDynamicParticleParameters();

	void OnGlobeTransformUpdated(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport);

	/** Places and sizes the ring on the fitted sphere (transform-level; called from RefreshSurface). */
	void RefreshCloudRing(const FVector& Center, const FQuat& Rotation, float GlobeSurfaceRadius);

	/** Smooths the audio input and pushes ring energy / intensity (called from Tick and on blend changes). */
	void UpdateRingEnergy(float DeltaSeconds);
	void PushRingLook();

	/** Places the waves at the fitted centre, measures their radius (world cm) and pushes layout / look parameters.
	 *  Restarts the system only when the path layout changes. */
	void RefreshPrisms(const FVector& Center, const FQuat& Rotation, float GlobeSurfaceRadius);

	/** Energy- and mode-dependent wave parameters (cheap; called on Energy and S-crossfade changes). */
	void PushPrismLook();

	/** Tick is needed while fading or while the ring's energy is animating in a game world. */
	bool NeedsTick() const;

	static float SimulatedSpeech(float Time);

	/** Transient: never saved into the level; rebuilt in OnConstruction/BeginPlay. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	float CurrentFade = 1.0f;
	float FadeStart = 1.0f;
	float FadeTarget = 1.0f;
	float FadeElapsed = 0.0f;
	/** > 0 while a fade is running. */
	float FadeDuration = 0.0f;

	float EarthModeBlend = 0.0f;

	float AudioEnergyInput = 0.0f;
	float SmoothedAudio = 0.0f;
	float RingEnergy = 0.0f;
	float LastPushedRingEnergy = -1.0f;
	float RingGlobeRadius = 0.0f;
	float RingRadiusApplied = 1300.0f;
	/** Mist component orientation without spin, and the accumulated spin angle (degrees). */
	FQuat RingSpinBase = FQuat::Identity;
	float RingSpinAngle = 0.0f;

	/** Globe orientation captured when play starts; the non-following ring keeps using it. */
	FQuat RingBaseRotation = FQuat::Identity;
	bool bRingBaseRotationCaptured = false;

	FQuat PrismBaseRotation = FQuat::Identity;
	/** Ribbon component rotation without spin (relative space, like RingSpinBase). */
	FQuat PrismSpinBase = FQuat::Identity;
	float PrismSpinAngle = 0.0f;
	/** Globe orientation when play started (Reset Globe Rotation). */
	FQuat PlacedGlobeRotation = FQuat::Identity;

	/** Places / sizes the city-light shell on the earth and pushes its look. */
	void RefreshCityLights(const FVector& Center, const FQuat& Rotation);
	void PushCityLightsLook();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CityLightsMID;
	TWeakObjectPtr<class ADirectionalLight> CachedSun;
	FVector LastSunDirection = FVector(1.0e9);
	bool bPrismBaseRotationCaptured = false;
	/** On/off ramp, linear 0..1 (pushed eased); moves toward bEnablePrisms in Tick. */
	float PrismRamp = 0.0f;
	/** Shared wave clock (seconds x speed), wrapped at GlobeVFX::PrismParams::ClockPeriod. Pushed to both the sheet
	 *  material and the sparkles, so the sparkles sit on the moving sheet. */
	double PrismClock = 0.0;
	/** The flutter's own clock, and both clocks' current rates (clock units per second) for the trail lag. */
	double FlutterClock = 0.0;
	float PrismClockRate = 1.0f;
	float FlutterClockRate = 1.0f;
	float SmoothedPrismEnergy = -1.0f;

	/** Shows / hides the ribbon and activates / deactivates the sparkles for the current on/off state and ramp. */
	void ApplyPrismActivation();
	/** Ramp, energy smoothing and the wave clock (game world only). */
	void UpdatePrisms(float DeltaSeconds);

	/** Sprite material last pushed to Niagara (avoids re-binding it every crossfade frame). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> AppliedSpriteMaterial;

	/** Points materials with the oceans switched off (Points On Land Only). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LandPointsMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LandPointsOnTopMID;

	/** Pushes the earth's axes (in the hologram shell's local space) and the continents look to the shell. */
	void PushHologramLand();
	const UStaticMeshComponent* FindEarthMesh() const;
	float SurfaceRadius = 0.0f;
	FVector LastRootScale = FVector::ZeroVector;
	FDelegateHandle TransformUpdatedHandle;
};
