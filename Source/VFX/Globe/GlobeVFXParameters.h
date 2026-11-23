// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Names shared between the runtime actor (AGlobeVFXActor) and the editor-only generators
 * (VFXEditor module). Keep them in sync with the generated assets; renaming one breaks the binding silently.
 */
namespace GlobeVFX::Params
{
	// M_GlobeHologram
	inline constexpr const TCHAR* GlobeColor = TEXT("GlobeColor");
	inline constexpr const TCHAR* GlowIntensity = TEXT("GlowIntensity");
	inline constexpr const TCHAR* Energy = TEXT("Energy");
	inline constexpr const TCHAR* FresnelExponent = TEXT("FresnelExponent");
	inline constexpr const TCHAR* VariationAmount = TEXT("VariationAmount");
	inline constexpr const TCHAR* VariationSpeed = TEXT("VariationSpeed");
	/** 1 = fully visible, 0 = invisible. Driven by AGlobeVFXActor::FadeTo. */
	inline constexpr const TCHAR* Fade = TEXT("Fade");
	/** Overall hologram transparency multiplier (AGlobeVFXActor::HologramOpacity). */
	inline constexpr const TCHAR* OpacityScale = TEXT("OpacityScale");
	/** Opacity = saturate(Fresnel * RimOpacity * Variation + CoreOpacity) * OpacityScale * Fade. */
	inline constexpr const TCHAR* RimOpacity = TEXT("RimOpacity");
	inline constexpr const TCHAR* CoreOpacity = TEXT("CoreOpacity");

	// M_GlobeHologram continents layer (land in the hologram's colour, aligned with the real earth).
	inline constexpr const TCHAR* LandColor = TEXT("LandColor");
	inline constexpr const TCHAR* LandFill = TEXT("LandFill");           // land brightness
	inline constexpr const TCHAR* LandOpacity = TEXT("LandOpacity");     // how solid the land reads on the shell
	inline constexpr const TCHAR* LandCoastGlow = TEXT("LandCoastGlow"); // coastline brightness
	inline constexpr const TCHAR* LandFade = TEXT("LandFade");           // 1 = shown (hologram mode), 0 = gone (earth mode)
	// The earth mesh's local X / Y / Z axes in the hologram shell's local space (land mask frame).
	inline constexpr const TCHAR* LandAxisX = TEXT("LandAxisX");
	inline constexpr const TCHAR* LandAxisY = TEXT("LandAxisY");
	inline constexpr const TCHAR* LandAxisZ = TEXT("LandAxisZ");

	// Centre clearing (particles via Dynamic Parameter 1; network via material instance):
	//   factor = 1 - Amount * smoothstep(Threshold - Softness, Threshold + Softness, |facing|)
	inline constexpr const TCHAR* CenterClearAmount = TEXT("CenterClearAmount");
	inline constexpr const TCHAR* CenterClearThreshold = TEXT("CenterClearThreshold");
	inline constexpr const TCHAR* CenterClearSoftness = TEXT("CenterClearSoftness");

	// M_GlobeParticle (sprite). Per-particle values arrive via Dynamic Parameter 0:
	//   x = ParticleIntensity, y = GlobalEnergy, z = BackSideVisibility, w = LandMaskStrength
	inline constexpr const TCHAR* BaseBrightness = TEXT("BaseBrightness");
	inline constexpr const TCHAR* OceanBrightness = TEXT("OceanBrightness");
	inline constexpr const TCHAR* TwinkleAmount = TEXT("TwinkleAmount");
	inline constexpr const TCHAR* LandMask = TEXT("LandMask");

	// M_GlobeNetwork (procedural link/node mesh)
	inline constexpr const TCHAR* NetworkColor = TEXT("NetworkColor");
	inline constexpr const TCHAR* NetworkIntensity = TEXT("NetworkIntensity");
	inline constexpr const TCHAR* PulseSpeed = TEXT("PulseSpeed");
	inline constexpr const TCHAR* GrowCycle = TEXT("GrowCycle");     // seconds per draw-hold-fade cycle
	inline constexpr const TCHAR* GrowEnabled = TEXT("GrowEnabled"); // 0 = static web, 1 = links keep extending

	// M_GlobeCloudCore (Phase 3 ring core, procedural band mesh). UV.x = angle around the ring (0..1, seamless),
	// UV.y = across the band (0 bottom .. 1 top).
	inline constexpr const TCHAR* RingColor = TEXT("RingColor");
	inline constexpr const TCHAR* RingIntensity = TEXT("RingIntensity"); // mode blend x enable (0 = invisible)
	inline constexpr const TCHAR* RingEnergy = TEXT("RingEnergy");       // smoothed audio + global energy, 0..1
	inline constexpr const TCHAR* WaveHeight = TEXT("WaveHeight");       // peak amplitude, fraction of the band height
	inline constexpr const TCHAR* LineWidth = TEXT("LineWidth");         // luminous line half-width, fraction of band
	inline constexpr const TCHAR* VaporStrength = TEXT("VaporStrength");
	inline constexpr const TCHAR* RingExpand = TEXT("RingExpand");       // cm outward at full energy (WPO)
	inline constexpr const TCHAR* WaveSpeed = TEXT("WaveSpeed");
	inline constexpr const TCHAR* RingSpin = TEXT("RingSpin");           // revolutions per second (pattern slides around)

	// M_GlobeMist (Phase 3 mist sprites). Dynamic Parameter 0 = (RingIntensity, AudioEnergy, ClearAmount, ClearThreshold).
	inline constexpr const TCHAR* MistBrightness = TEXT("MistBrightness");

	// M_GlobePrismSheet (the ribbon; its other parameters are named in GlobeVFX::PrismParams) and M_GlobePrismSprite
	// (sparkles: Dynamic Parameter 0 = (0, Intensity, OccluderRadius, BackSideVisibility), 2 = (FrontVisibility, ...)).
	inline constexpr const TCHAR* PrismEmissive = TEXT("PrismEmissive");     // overall emissive scale (bloom control)

	// M_GlobeCityLights (night-side city lights on a shell just above the earth).
	inline constexpr const TCHAR* SunDirection = TEXT("SunDirection");       // world direction toward the sun (0 = all night)
	inline constexpr const TCHAR* CityColor = TEXT("CityColor");
	inline constexpr const TCHAR* CityColorB = TEXT("CityColorB");
	inline constexpr const TCHAR* CityIntensity = TEXT("CityIntensity");
	inline constexpr const TCHAR* CityDensity = TEXT("CityDensity");         // 0..1 share of land cells with a light
	inline constexpr const TCHAR* CitySizeMin = TEXT("CitySizeMin");         // light radius range, fraction of a grid cell
	inline constexpr const TCHAR* CitySizeMax = TEXT("CitySizeMax");
	inline constexpr const TCHAR* CityBigShare = TEXT("CityBigShare");       // share of lights near Size Max (big cities)
	inline constexpr const TCHAR* CityHalo = TEXT("CityHalo");               // soft glow around each light's core
	inline constexpr const TCHAR* CityScale = TEXT("CityScale");             // grid fineness (1 = 360 x 180 towns)
	inline constexpr const TCHAR* CityBlink = TEXT("CityBlink");             // share of its cycle a blinking light is off
	inline constexpr const TCHAR* CityBlinkSpeed = TEXT("CityBlinkSpeed");   // blink cycles per second
	inline constexpr const TCHAR* CityGlow = TEXT("CityGlow");               // faint urban haze over lit land
	inline constexpr const TCHAR* CityNightSoftness = TEXT("CityNightSoftness"); // width of the day/night terminator
	inline constexpr const TCHAR* CityFade = TEXT("CityFade");               // mode blend x enable
	inline constexpr const TCHAR* CityNightDarkness = TEXT("CityNightDarkness"); // how much the night side is darkened
}

/** Niagara User parameters on NS_GlobeSurface (names without the "User." namespace). */
namespace GlobeVFX::NiagaraParams
{
	inline constexpr const TCHAR* SurfaceRadius = TEXT("SurfaceRadius");
	/** Band thickness as a fraction of SurfaceRadius, growing inward (Shape Location defaults). 0 = one thin shell. */
	inline constexpr const TCHAR* SurfaceHeightVariation = TEXT("SurfaceHeightVariation");
	/** Shape Location V2 "Sphere Surface Distribution": 1 = on the surface, 0 = through the volume (its default). */
	inline constexpr const TCHAR* SphereSurfaceDistribution = TEXT("SphereSurfaceDistribution");
	inline constexpr const TCHAR* GlobalEnergy = TEXT("GlobalEnergy");
	inline constexpr const TCHAR* ParticleIntensity = TEXT("ParticleIntensity");
	inline constexpr const TCHAR* GlowColor = TEXT("GlowColor");
	inline constexpr const TCHAR* SpawnRate = TEXT("SpawnRate");
	inline constexpr const TCHAR* ParticleLifetime = TEXT("ParticleLifetime");
	inline constexpr const TCHAR* SpriteSize = TEXT("SpriteSize");
	inline constexpr const TCHAR* FlowSpeed = TEXT("FlowSpeed");
	inline constexpr const TCHAR* BackSideVisibility = TEXT("BackSideVisibility");
	inline constexpr const TCHAR* LandMaskStrength = TEXT("LandMaskStrength");
	/** Sprite renderer material (bound via the renderer's Material User Param Binding). */
	inline constexpr const TCHAR* SpriteMaterial = TEXT("SpriteMaterial");
	// -> Dynamic Parameter 1 (x, y, z) in M_GlobeParticle.
	inline constexpr const TCHAR* CenterClearAmount = TEXT("CenterClearAmount");
	inline constexpr const TCHAR* CenterClearThreshold = TEXT("CenterClearThreshold");
	inline constexpr const TCHAR* CenterClearSoftness = TEXT("CenterClearSoftness");
}

/** Niagara User parameters on NS_GlobeCloudWave (the ring mist; names without "User."). GlowColor is shared. */
namespace GlobeVFX::CloudParams
{
	inline constexpr const TCHAR* AudioEnergy = TEXT("AudioEnergy");     // smoothed ring energy 0..1
	inline constexpr const TCHAR* RingIntensity = TEXT("RingIntensity"); // mode blend x enable
	inline constexpr const TCHAR* RingRadius = TEXT("RingRadius");       // torus centre-line radius (cm)
	inline constexpr const TCHAR* RingThickness = TEXT("RingThickness"); // torus tube radius (cm)
	inline constexpr const TCHAR* CloudDensity = TEXT("CloudDensity");   // mist particles per second
	inline constexpr const TCHAR* MistSize = TEXT("MistSize");
	inline constexpr const TCHAR* MistLifetime = TEXT("MistLifetime");
	inline constexpr const TCHAR* MistTurbulence = TEXT("MistTurbulence"); // curl noise strength
	inline constexpr const TCHAR* MistNoiseFrequency = TEXT("MistNoiseFrequency");
	inline constexpr const TCHAR* MistDrag = TEXT("MistDrag");
	inline constexpr const TCHAR* MistSwirl = TEXT("MistSwirl");         // cm/s drift around the ring
	// -> Dynamic Parameter 0 z/w in M_GlobeMist: centre clearing while the earth is shown.
	inline constexpr const TCHAR* RingClearAmount = TEXT("RingClearAmount");
	inline constexpr const TCHAR* RingClearThreshold = TEXT("RingClearThreshold");
}

/**
 * Niagara User parameters on NS_GlobeDataLink (one instance per data link; names without "User.").
 * Positions are in the component's space: the component sits at the globe centre with world axes.
 * M_GlobeLink reads Dynamic Parameter 0 = (LinkIntensity, lateral width, OccluderRadius, mode 0/1/2).
 */
namespace GlobeVFX::LinkParams
{
	inline constexpr const TCHAR* P0 = TEXT("LinkP0");                   // start anchor (behind the globe)
	inline constexpr const TCHAR* P1 = TEXT("LinkP1");                   // handle 1
	inline constexpr const TCHAR* P2 = TEXT("LinkP2");                   // handle 2
	inline constexpr const TCHAR* P3 = TEXT("LinkP3");                   // endpoint node
	inline constexpr const TCHAR* LinkColor = TEXT("LinkColor");
	inline constexpr const TCHAR* LinkIntensity = TEXT("LinkIntensity"); // active fade 0..1 x brightness
	inline constexpr const TCHAR* OccluderRadius = TEXT("OccluderRadius");
	inline constexpr const TCHAR* StreamRate = TEXT("StreamRate");
	inline constexpr const TCHAR* StreamLifetime = TEXT("StreamLifetime"); // seconds to travel start -> end
	inline constexpr const TCHAR* StreamSize = TEXT("StreamSize");
	inline constexpr const TCHAR* StreamWidth = TEXT("StreamWidth");
	inline constexpr const TCHAR* FilamentRate = TEXT("FilamentRate");
	inline constexpr const TCHAR* FilamentSize = TEXT("FilamentSize");
	inline constexpr const TCHAR* FilamentSpread = TEXT("FilamentSpread");
	inline constexpr const TCHAR* NodeRate = TEXT("NodeRate");
	inline constexpr const TCHAR* NodeSize = TEXT("NodeSize");
	inline constexpr const TCHAR* NodeT = TEXT("NodeT");                 // curve position of the node (1 = end)
	inline constexpr const TCHAR* NodeLifetime = TEXT("NodeLifetime");
	inline constexpr const TCHAR* FilamentLifetime = TEXT("FilamentLifetime");
	inline constexpr const TCHAR* NodeWidth = TEXT("NodeWidth");         // 0: node sprites stay on the point
	// Country colour flow: particles in the country's colours swirling around the white stream (M_GlobeLink mode 3).
	inline constexpr const TCHAR* CountryRate = TEXT("CountryRate");
	inline constexpr const TCHAR* CountryLifetime = TEXT("CountryLifetime");
	inline constexpr const TCHAR* CountrySize = TEXT("CountrySize");
	inline constexpr const TCHAR* CountrySpread = TEXT("CountrySpread");   // radius of the swirl around the core (cm)
	inline constexpr const TCHAR* CountryIntensity = TEXT("CountryIntensity");
	inline constexpr const TCHAR* CountryColorA = TEXT("CountryColorA");
	inline constexpr const TCHAR* CountryColorB = TEXT("CountryColorB");
	inline constexpr const TCHAR* CountryColorC = TEXT("CountryColorC");
	inline constexpr const TCHAR* CountryColorCount = TEXT("CountryColorCount"); // 1..3 colours in use
	// Node and card loop: the node glows behind the card centre; loop particles leave the link's end (the card's inner
	// edge) and run around a rounded rectangle the size of the card, both ways (M_GlobeLink mode 4).
	inline constexpr const TCHAR* NodePosition = TEXT("NodePosition");     // component space
	inline constexpr const TCHAR* NodeIntensity = TEXT("NodeIntensity");   // node brightness (dimmed behind the card)
	inline constexpr const TCHAR* CardCenter = TEXT("CardCenter");         // component space
	inline constexpr const TCHAR* CardHalfRight = TEXT("CardHalfRight");   // camera right x half width (cm)
	inline constexpr const TCHAR* CardHalfUp = TEXT("CardHalfUp");         // camera up x half height (cm)
	inline constexpr const TCHAR* LoopStartAngle = TEXT("LoopStartAngle"); // radians: 0 = right edge, pi = left edge
	inline constexpr const TCHAR* LoopRate = TEXT("LoopRate");
	inline constexpr const TCHAR* LoopLifetime = TEXT("LoopLifetime");
	inline constexpr const TCHAR* LoopSize = TEXT("LoopSize");
	inline constexpr const TCHAR* LoopJitter = TEXT("LoopJitter");
	inline constexpr const TCHAR* LoopIntensity = TEXT("LoopIntensity");
	inline constexpr const TCHAR* LoopRoundness = TEXT("LoopRoundness");   // superellipse exponent (2 = ellipse, higher = squarer)
	inline constexpr const TCHAR* LoopTravel = TEXT("LoopTravel");         // 1 = each particle reaches the far side
	// Constant per-emitter modes for M_GlobeLink (0 stream, 1 filament lanes, 2 node glow, 3 country swirl, 4 card loop).
	inline constexpr const TCHAR* ModeStream = TEXT("ModeStream");
	inline constexpr const TCHAR* ModeFilament = TEXT("ModeFilament");
	inline constexpr const TCHAR* ModeNode = TEXT("ModeNode");
	inline constexpr const TCHAR* ModeCountry = TEXT("ModeCountry");
	inline constexpr const TCHAR* ModeLoop = TEXT("ModeLoop");
}

/**
 * Prisms: the PS3-XMB-style ribbon around the globe. These names are shared by the scalar/vector parameters of
 * M_GlobePrismSheet (set on the ribbon's dynamic material) and the User parameters of NS_GlobePrisms (the sparkles).
 * All lengths are world cm: both components sit at the globe centre with absolute scale 1, and AGlobeVFXActor
 * measures the radius from the scaled mesh bounds, so actor scale is applied exactly once.
 */
namespace GlobeVFX::PrismParams
{
	inline constexpr const TCHAR* RibbonClock = TEXT("RibbonClock");       // shared wave clock (s), wraps at ClockPeriod
	inline constexpr const TCHAR* RibbonInner = TEXT("RibbonInner");       // inner edge radius (cm)
	inline constexpr const TCHAR* RibbonOuter = TEXT("RibbonOuter");       // outer edge radius (cm)
	inline constexpr const TCHAR* RibbonAmp = TEXT("RibbonAmp");           // wave height (cm), energy and ramp applied
	inline constexpr const TCHAR* RibbonLayers = TEXT("RibbonLayers");     // number of sheets (float)
	inline constexpr const TCHAR* RibbonEdgeGlow = TEXT("RibbonEdgeGlow");
	inline constexpr const TCHAR* RibbonFill = TEXT("RibbonFill");
	inline constexpr const TCHAR* RibbonLineGlow = TEXT("RibbonLineGlow");   // trail lines along the sheets (any view angle)
	inline constexpr const TCHAR* RibbonLineWidth = TEXT("RibbonLineWidth"); // their width, fraction of the band
	inline constexpr const TCHAR* RibbonLineHalo = TEXT("RibbonLineHalo");   // soft glow around them
	inline constexpr const TCHAR* OccluderRadius = TEXT("OccluderRadius"); // visible globe radius (cm)
	inline constexpr const TCHAR* BackSideVisibility = TEXT("BackSideVisibility"); // brightness where the globe covers it
	inline constexpr const TCHAR* FrontVisibility = TEXT("FrontVisibility"); // 0 = only behind the globe, 1 = also in front
	inline constexpr const TCHAR* PrismIntensity = TEXT("PrismIntensity"); // overall brightness (mode blend, energy applied)
	inline constexpr const TCHAR* BlueColor = TEXT("BlueColor");
	inline constexpr const TCHAR* GoldColor = TEXT("GoldColor");
	// On/off ramp (R key): 0 = off, 1 = full. Band width and wave height grow from RampMinScale to 1; brightness fades
	// in over the first quarter.
	inline constexpr const TCHAR* PrismRamp = TEXT("PrismRamp");
	inline constexpr const TCHAR* RampMinScale = TEXT("RampMinScale");
	// Cloth look: flutter ripples and the soft trail (echo copies of each sheet that run behind in time).
	inline constexpr const TCHAR* RibbonFlutter = TEXT("RibbonFlutter");             // flutter height relative to the waves
	inline constexpr const TCHAR* RibbonFlutterClock = TEXT("RibbonFlutterClock");   // the flutter's own clock (wraps at ClockPeriod)
	inline constexpr const TCHAR* RibbonTrailLag = TEXT("RibbonTrailLag");           // RibbonClock units the last echo lags behind
	inline constexpr const TCHAR* RibbonTrailFlutterLag = TEXT("RibbonTrailFlutterLag"); // same, in RibbonFlutterClock units
	inline constexpr const TCHAR* RibbonTrailOpacity = TEXT("RibbonTrailOpacity");   // brightness of the first echo
	// Sparkles (NS_GlobePrisms).
	inline constexpr const TCHAR* SparkleRate = TEXT("SparkleRate");       // per second
	inline constexpr const TCHAR* SparkleLifetime = TEXT("SparkleLifetime");
	inline constexpr const TCHAR* SparkleSize = TEXT("SparkleSize");       // cm
	inline constexpr const TCHAR* SparkleDrift = TEXT("SparkleDrift");     // turns per second along the ring
	inline constexpr const TCHAR* SparkleIntensity = TEXT("SparkleIntensity");

	/** RibbonClock wraps at this period; every wave's time rate is a whole number of cycles per period, so the wrap
	 *  is invisible and float precision never degrades over a long session. */
	inline constexpr float ClockPeriod = 1000.0f;

	/**
	 * Wave height (local Z, cm) of a sheet as an HLSL expression, shared by the material (World Position Offset and
	 * normals) and the sparkles, so both follow the same surface. U = angle around the ring 0..1 (seamless: whole
	 * cycles around), V = across the band 0 inner..1 outer, Seed = the sheet's 0..1 seed (different phases make the
	 * sheets cross like the XMB ribbon), Amp = height in cm. The outer edge swings more than the inner one.
	 * Terms: a * sin(2pi (n U + m Clock / Period) + 2pi p Seed + c V).
	 */
	struct FWaveTerm { float N, M, A, P, C; };
	inline constexpr FWaveTerm WaveTerms[] = { { 2.0f, 50.0f, 0.55f, 1.0f, 1.3f }, { 3.0f, -37.0f, 0.30f, 1.7f, 2.4f }, { 5.0f, 71.0f, 0.15f, 2.3f, -3.1f } };

	/**
	 * Cloth flutter: smaller, faster ripples on top (scaled by RibbonFlutter), stronger toward the outer edge like loose
	 * fabric. Same form as WaveTerms; whole cycles per clock period, so the clock wrap stays invisible.
	 */
	inline constexpr FWaveTerm FlutterTerms[] = { { 4.0f, 700.0f, 0.45f, 2.9f, 5.2f }, { 7.0f, -1000.0f, 0.32f, 0.7f, 3.1f }, { 11.0f, 1400.0f, 0.2f, 1.9f, -4.3f } };

	inline FString WaveArg(const FWaveTerm& T, const FString& U, const FString& V, const FString& Clock, const FString& Seed)
	{
		return FString::Printf(TEXT("(6.2831853 * (%.1f * %s + %.1f * %s / %.1f) + 6.2831853 * %.2f * %s + %.2f * %s)"),
			T.N, *U, T.M, *Clock, ClockPeriod, T.P, *Seed, T.C, *V);
	}

	/** Sum of a_k * Fn(arg_k) * Weight_k, Fn = "sin" or "cos", Weight = "1", "n" (d/dU / 2pi) or "c" (d/dV). */
	template <int32 Count>
	FString TermSum(const FWaveTerm (&Terms)[Count], const TCHAR* Fn, const TCHAR* Weight, const FString& U, const FString& V, const FString& Clock, const FString& Seed)
	{
		FString Sum;
		for (int32 K = 0; K < Count; ++K)
		{
			const FWaveTerm& T = Terms[K];
			const float W = FCString::Strcmp(Weight, TEXT("n")) == 0 ? T.N : FCString::Strcmp(Weight, TEXT("c")) == 0 ? T.C : 1.0f;
			Sum += FString::Printf(TEXT("%s%.3f * %s(%s)"), K ? TEXT(" + ") : TEXT(""), T.A * W, Fn, *WaveArg(T, U, V, Clock, Seed));
		}
		return TEXT("(") + Sum + TEXT(")");
	}

	inline FString WaveSum(const TCHAR* Fn, const TCHAR* Weight, const FString& U, const FString& V, const FString& Clock, const FString& Seed)
	{
		return TermSum(WaveTerms, Fn, Weight, U, V, Clock, Seed);
	}

	inline FString FlutterSum(const TCHAR* Fn, const TCHAR* Weight, const FString& U, const FString& V, const FString& Clock, const FString& Seed)
	{
		return TermSum(FlutterTerms, Fn, Weight, U, V, Clock, Seed);
	}

	/**
	 * h = Amp * ((0.55 + 0.45 V) * waves + (Seed - 0.5) * 0.15 + Flutter * (0.25 + 0.75 V) * flutter).
	 * (The material's normal uses the matching derivatives: see BuildPrismSheetGraph.)
	 */
	inline FString WaveHeightHlsl(const FString& U, const FString& V, const FString& Clock, const FString& Seed, const FString& Amp, const FString& Flutter,
		const FString& FlutterClock)
	{
		return FString::Printf(TEXT("(%s * ((0.55 + 0.45 * %s) * %s + (%s - 0.5) * 0.15 + %s * (0.25 + 0.75 * %s) * %s))"), *Amp, *V,
			*WaveSum(TEXT("sin"), TEXT("1"), U, V, Clock, Seed), *Seed, *Flutter, *V, *FlutterSum(TEXT("sin"), TEXT("1"), U, V, FlutterClock, Seed));
	}
}

namespace GlobeVFX::Assets
{
	inline constexpr const TCHAR* MaterialFolder = TEXT("/Game/AIGlobe/Materials");
	inline constexpr const TCHAR* ParentMaterialName = TEXT("M_GlobeHologram");
	inline constexpr const TCHAR* MaterialInstanceName = TEXT("MI_GlobeHologram");
	inline constexpr const TCHAR* ParticleMaterialName = TEXT("M_GlobeParticle");
	inline constexpr const TCHAR* ParticleInstanceName = TEXT("MI_GlobeParticle");
	/** Same sprite with depth test off: points draw over the opaque earth regardless of their exact height. */
	inline constexpr const TCHAR* ParticleOnTopMaterialName = TEXT("M_GlobeParticleOnTop");
	inline constexpr const TCHAR* ParticleOnTopInstanceName = TEXT("MI_GlobeParticleOnTop");
	inline constexpr const TCHAR* NetworkMaterialName = TEXT("M_GlobeNetwork");
	inline constexpr const TCHAR* NetworkInstanceName = TEXT("MI_GlobeNetwork");

	inline constexpr const TCHAR* NiagaraFolder = TEXT("/Game/AIGlobe/Niagara");
	inline constexpr const TCHAR* SurfaceSystemName = TEXT("NS_GlobeSurface");
	inline constexpr const TCHAR* CloudWaveSystemName = TEXT("NS_GlobeCloudWave");
	inline constexpr const TCHAR* CloudCoreMaterialName = TEXT("M_GlobeCloudCore");
	inline constexpr const TCHAR* CloudCoreInstanceName = TEXT("MI_GlobeCloudCore");
	inline constexpr const TCHAR* MistMaterialName = TEXT("M_GlobeMist");
	inline constexpr const TCHAR* MistInstanceName = TEXT("MI_GlobeMist");
	inline constexpr const TCHAR* DataLinkSystemName = TEXT("NS_GlobeDataLink");
	inline constexpr const TCHAR* LinkMaterialName = TEXT("M_GlobeLink");
	inline constexpr const TCHAR* LinkInstanceName = TEXT("MI_GlobeLink");
	inline constexpr const TCHAR* PrismsSystemName = TEXT("NS_GlobePrisms");
	inline constexpr const TCHAR* PrismSheetMaterialName = TEXT("M_GlobePrismSheet");
	inline constexpr const TCHAR* PrismSheetInstanceName = TEXT("MI_GlobePrismSheet");
	inline constexpr const TCHAR* PrismSpriteMaterialName = TEXT("M_GlobePrismSprite");
	inline constexpr const TCHAR* PrismSpriteInstanceName = TEXT("MI_GlobePrismSprite");
	inline constexpr const TCHAR* CityLightsMaterialName = TEXT("M_GlobeCityLights");
	inline constexpr const TCHAR* CityLightsInstanceName = TEXT("MI_GlobeCityLights");

	inline constexpr const TCHAR* TextureFolder = TEXT("/Game/AIGlobe/Textures");
	inline constexpr const TCHAR* LandMaskName = TEXT("T_GlobeLandMask");
	inline constexpr const TCHAR* CityMaskName = TEXT("T_GlobeCityMask");

	inline constexpr const TCHAR* UIFolder = TEXT("/Game/AIGlobe/UI");
	inline constexpr const TCHAR* VoiceVisualizerWidgetName = TEXT("WBP_GlobeVoiceVisualizer");
	inline constexpr const TCHAR* StatCardWidgetName = TEXT("WBP_GlobeStatCard");
}
