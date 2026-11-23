// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeMaterialGenerator.h"

#include "Engine/Texture2D.h"
#include "Globe/GlobeAssetUtils.h"
#include "Globe/GlobeLandMaskBaker.h"
#include "Globe/GlobeVFXParameters.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionArcsine.h"
#include "Materials/MaterialExpressionArctangent2.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionObjectPositionWS.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionParticlePositionWS.h"
#include "Materials/MaterialExpressionParticleRandom.h"
#include "Materials/MaterialExpressionParticleRelativeTime.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeMaterialGenerator, Log, All);

namespace
{
	const FName ParamGroup(TEXT("Globe"));

	template <typename TExpression>
	TExpression* AddExpression(UMaterial* Material, int32 X, int32 Y)
	{
		return CastChecked<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(Material, TExpression::StaticClass(), X, Y));
	}

	UMaterialExpressionScalarParameter* AddScalarParam(UMaterial* Material, const TCHAR* Name, float Default, float SliderMin, float SliderMax, int32 X, int32 Y)
	{
		UMaterialExpressionScalarParameter* Param = AddExpression<UMaterialExpressionScalarParameter>(Material, X, Y);
		Param->ParameterName = FName(Name);
		Param->Group = ParamGroup;
		Param->DefaultValue = Default;
		Param->SliderMin = SliderMin;
		Param->SliderMax = SliderMax;
		return Param;
	}

	UMaterialExpressionMultiply* AddMultiply(UMaterial* Material, UMaterialExpression* A, UMaterialExpression* B, int32 X, int32 Y)
	{
		UMaterialExpressionMultiply* Node = AddExpression<UMaterialExpressionMultiply>(Material, X, Y);
		Node->A.Connect(0, A);
		Node->B.Connect(0, B);
		return Node;
	}

	UMaterialExpressionMultiply* AddMultiplyConst(UMaterial* Material, UMaterialExpression* A, float ConstB, int32 X, int32 Y)
	{
		UMaterialExpressionMultiply* Node = AddExpression<UMaterialExpressionMultiply>(Material, X, Y);
		Node->A.Connect(0, A);
		Node->ConstB = ConstB;
		return Node;
	}

	UMaterialExpressionAdd* AddAdd(UMaterial* Material, UMaterialExpression* A, UMaterialExpression* B, int32 X, int32 Y)
	{
		UMaterialExpressionAdd* Node = AddExpression<UMaterialExpressionAdd>(Material, X, Y);
		Node->A.Connect(0, A);
		Node->B.Connect(0, B);
		return Node;
	}

	UMaterialExpressionAdd* AddAddConst(UMaterial* Material, UMaterialExpression* A, float ConstB, int32 X, int32 Y)
	{
		UMaterialExpressionAdd* Node = AddExpression<UMaterialExpressionAdd>(Material, X, Y);
		Node->A.Connect(0, A);
		Node->ConstB = ConstB;
		return Node;
	}

	/** An expression output: node + output index (e.g. a Dynamic Parameter channel or a scalar parameter). */
	struct FOut
	{
		UMaterialExpression* Node;
		int32 Index = 0;
	};

	/**
	 * Centre clearing so the globe's texture reads through the middle of the view:
	 *   1 - Amount * smoothstep(Threshold - Softness, Threshold + Softness, |Facing|)
	 * Facing = dot(direction from sphere centre, direction to camera): 1 at the centre of the disc, 0 at the rim.
	 * |Facing| treats far-side points behind the centre the same way (visible through the hologram).
	 */
	UMaterialExpression* AddCenterClear(UMaterial* Material, UMaterialExpression* Facing, FOut Amount, FOut Threshold, FOut Softness, int32 X, int32 Y)
	{
		UMaterialExpressionAbs* AbsFacing = AddExpression<UMaterialExpressionAbs>(Material, X, Y);
		AbsFacing->Input.Connect(0, Facing);
		UMaterialExpressionSubtract* Low = AddExpression<UMaterialExpressionSubtract>(Material, X, Y + 100);
		Low->A.Connect(Threshold.Index, Threshold.Node);
		Low->B.Connect(Softness.Index, Softness.Node);
		UMaterialExpressionAdd* High = AddExpression<UMaterialExpressionAdd>(Material, X, Y + 200);
		High->A.Connect(Threshold.Index, Threshold.Node);
		High->B.Connect(Softness.Index, Softness.Node);
		UMaterialExpressionSmoothStep* Inside = AddExpression<UMaterialExpressionSmoothStep>(Material, X + 150, Y + 100);
		Inside->Min.Connect(0, Low);
		Inside->Max.Connect(0, High);
		Inside->Value.Connect(0, AbsFacing);
		UMaterialExpressionMultiply* Scaled = AddExpression<UMaterialExpressionMultiply>(Material, X + 300, Y + 100);
		Scaled->A.Connect(Amount.Index, Amount.Node);
		Scaled->B.Connect(0, Inside);
		UMaterialExpressionOneMinus* Keep = AddExpression<UMaterialExpressionOneMinus>(Material, X + 450, Y + 100);
		Keep->Input.Connect(0, Scaled);
		return Keep;
	}

	/** sin(2*pi*(dot(N, Dir) * Frequency + Time * Drift)) — one soft travelling band across the sphere. */
	UMaterialExpression* AddBand(UMaterial* Material, UMaterialExpression* Normal, UMaterialExpression* ScaledTime,
		const FLinearColor& Dir, float Frequency, float Drift, int32 X, int32 Y)
	{
		UMaterialExpressionConstant3Vector* DirConst = AddExpression<UMaterialExpressionConstant3Vector>(Material, X, Y + 80);
		DirConst->Constant = Dir;

		UMaterialExpressionDotProduct* Dot = AddExpression<UMaterialExpressionDotProduct>(Material, X + 200, Y);
		Dot->A.Connect(0, Normal);
		Dot->B.Connect(0, DirConst);

		UMaterialExpression* Phase = AddMultiplyConst(Material, Dot, Frequency, X + 380, Y);
		UMaterialExpression* TimeTerm = AddMultiplyConst(Material, ScaledTime, Drift, X + 380, Y + 120);
		UMaterialExpression* Arg = AddAdd(Material, Phase, TimeTerm, X + 560, Y);

		UMaterialExpressionSine* Sine = AddExpression<UMaterialExpressionSine>(Material, X + 740, Y);
		Sine->Input.Connect(0, Arg);
		Sine->Period = 1.0f;
		return Sine;
	}

	void BuildGraph(UMaterial* Material)
	{
		using namespace GlobeVFX;

		// --- Parameters ---
		UMaterialExpressionVectorParameter* ColorParam = AddExpression<UMaterialExpressionVectorParameter>(Material, -1400, -400);
		ColorParam->ParameterName = FName(Params::GlobeColor);
		ColorParam->Group = ParamGroup;
		ColorParam->DefaultValue = FLinearColor(0.0f, 0.85f, 1.0f, 1.0f);

		UMaterialExpressionScalarParameter* GlowParam = AddScalarParam(Material, Params::GlowIntensity, 3.0f, 0.0f, 10.0f, -1400, -200);
		UMaterialExpressionScalarParameter* EnergyParam = AddScalarParam(Material, Params::Energy, 0.2f, 0.0f, 1.0f, -1400, -100);
		UMaterialExpressionScalarParameter* FresnelExpParam = AddScalarParam(Material, Params::FresnelExponent, 3.0f, 0.5f, 8.0f, -1400, 100);
		UMaterialExpressionScalarParameter* VarAmountParam = AddScalarParam(Material, Params::VariationAmount, 0.2f, 0.0f, 1.0f, -1400, 500);
		UMaterialExpressionScalarParameter* VarSpeedParam = AddScalarParam(Material, Params::VariationSpeed, 1.0f, 0.0f, 4.0f, -1400, 350);

		// --- Colour (RGB only; the parameter's first output is RGBA) ---
		UMaterialExpressionComponentMask* ColorRGB = AddExpression<UMaterialExpressionComponentMask>(Material, -1150, -400);
		ColorRGB->Input.Connect(0, ColorParam);
		ColorRGB->R = ColorRGB->G = ColorRGB->B = 1;
		ColorRGB->A = 0;

		// --- Fresnel rim ---
		UMaterialExpressionFresnel* Fresnel = AddExpression<UMaterialExpressionFresnel>(Material, -1150, 100);
		Fresnel->ExponentIn.Connect(0, FresnelExpParam);
		Fresnel->BaseReflectFraction = 0.04f;

		// --- Energy: gentle 0.6x (idle) .. 1.4x (full) brightness scale ---
		UMaterialExpressionLinearInterpolate* EnergyScale = AddExpression<UMaterialExpressionLinearInterpolate>(Material, -1150, -100);
		EnergyScale->ConstA = 0.6f;
		EnergyScale->ConstB = 1.4f;
		EnergyScale->Alpha.Connect(0, EnergyParam);

		// --- Animated surface variation: two crossing sine bands over the world normal. ---
		// ~a dozen ALU ops, no textures. Using the vertex normal keeps it independent of actor
		// position and scale, so the pattern stays locked to the sphere.
		UMaterialExpressionTime* Time = AddExpression<UMaterialExpressionTime>(Material, -2200, 350);
		UMaterialExpressionVertexNormalWS* Normal = AddExpression<UMaterialExpressionVertexNormalWS>(Material, -2200, 600);
		UMaterialExpression* ScaledTime = AddMultiply(Material, Time, VarSpeedParam, -2000, 350);

		UMaterialExpression* BandA = AddBand(Material, Normal, ScaledTime, FLinearColor(0.25f, 0.35f, 0.90f), 2.0f, 0.15f, -2000, 700);
		UMaterialExpression* BandB = AddBand(Material, Normal, ScaledTime, FLinearColor(0.85f, -0.45f, 0.25f), 3.0f, -0.11f, -2000, 1000);

		UMaterialExpression* Pattern = AddMultiply(Material, BandA, BandB, -1150, 800);        // -1..1
		UMaterialExpression* PatternScaled = AddMultiply(Material, Pattern, VarAmountParam, -950, 700);
		UMaterialExpression* Variation = AddAddConst(Material, PatternScaled, 1.0f, -750, 700); // ~1 +/- VariationAmount

		// --- Emissive = Color * Glow * EnergyScale * (Fresnel + 0.2 core) * Variation ---
		UMaterialExpression* RimTerm = AddAddConst(Material, Fresnel, 0.2f, -900, 100);
		UMaterialExpression* E1 = AddMultiply(Material, ColorRGB, GlowParam, -900, -350);
		UMaterialExpression* E2 = AddMultiply(Material, E1, EnergyScale, -700, -300);
		UMaterialExpression* E3 = AddMultiply(Material, E2, RimTerm, -500, -250);
		UMaterialExpression* Emissive = AddMultiply(Material, E3, Variation, -300, -200);

		// --- Opacity = saturate(Fresnel * Variation * RimOpacity + CoreOpacity) * OpacityScale ---
		UMaterialExpressionScalarParameter* RimOpacity = AddScalarParam(Material, Params::RimOpacity, 0.55f, 0.0f, 1.0f, -600, 450);
		UMaterialExpressionScalarParameter* CoreOpacity = AddScalarParam(Material, Params::CoreOpacity, 0.04f, 0.0f, 1.0f, -600, 550);
		UMaterialExpressionScalarParameter* OpacityScale = AddScalarParam(Material, Params::OpacityScale, 1.0f, 0.0f, 2.0f, -300, 450);
		UMaterialExpression* O1 = AddMultiply(Material, Fresnel, Variation, -600, 300);
		UMaterialExpression* O2 = AddMultiply(Material, O1, RimOpacity, -450, 300);
		UMaterialExpression* O3 = AddAdd(Material, O2, CoreOpacity, -300, 300);
		UMaterialExpressionSaturate* Opacity = AddExpression<UMaterialExpressionSaturate>(Material, -150, 300);
		Opacity->Input.Connect(0, O3);
		UMaterialExpressionSaturate* ScaledOpacity = AddExpression<UMaterialExpressionSaturate>(Material, 50, 400);
		ScaledOpacity->Input.Connect(0, AddMultiply(Material, Opacity, OpacityScale, -50, 400));

		// --- Centre clearing: while the earth shows inside the hologram, the middle of the shell turns clear so
		// the earth texture reads, and the Fresnel rim keeps glowing. Facing = dot(normal, direction to camera).
		UMaterialExpressionCameraVectorWS* ToCamera = AddExpression<UMaterialExpressionCameraVectorWS>(Material, -900, 650);
		UMaterialExpressionDotProduct* Facing = AddExpression<UMaterialExpressionDotProduct>(Material, -750, 650);
		Facing->A.Connect(0, Normal);
		Facing->B.Connect(0, ToCamera);
		UMaterialExpressionScalarParameter* ClearAmount = AddScalarParam(Material, Params::CenterClearAmount, 0.0f, 0.0f, 1.0f, -750, 750);
		UMaterialExpressionScalarParameter* ClearThreshold = AddScalarParam(Material, Params::CenterClearThreshold, 0.87f, 0.0f, 1.0f, -750, 850);
		UMaterialExpressionScalarParameter* ClearSoftness = AddScalarParam(Material, Params::CenterClearSoftness, 0.12f, 0.0f, 0.5f, -750, 950);
		UMaterialExpression* CenterKeep = AddCenterClear(Material, Facing, { ClearAmount, 0 }, { ClearThreshold, 0 }, { ClearSoftness, 0 }, -550, 700);

		// --- Fade (1 = visible, 0 = gone) scales both outputs so the shell dissolves evenly ---
		UMaterialExpressionScalarParameter* FadeParam = AddScalarParam(Material, Params::Fade, 1.0f, 0.0f, 1.0f, -300, 0);
		UMaterialExpression* Visibility = AddMultiply(Material, FadeParam, CenterKeep, -150, 100);
		UMaterialExpression* FadedEmissive = AddMultiply(Material, Emissive, Visibility, -100, -200);
		UMaterialExpression* FadedOpacity = AddMultiply(Material, ScaledOpacity, Visibility, 150, 300);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		EditorData->EmissiveColor.Connect(0, FadedEmissive);
		EditorData->Opacity.Connect(0, FadedOpacity);
	}


	// ------------------------------------------------------------------ shared small helpers

	UMaterialExpressionSubtract* AddSubtract(UMaterial* Material, UMaterialExpression* A, UMaterialExpression* B, int32 X, int32 Y)
	{
		UMaterialExpressionSubtract* Node = AddExpression<UMaterialExpressionSubtract>(Material, X, Y);
		Node->A.Connect(0, A);
		Node->B.Connect(0, B);
		return Node;
	}

	UMaterialExpressionLinearInterpolate* AddLerpConst(UMaterial* Material, float A, float B, UMaterialExpression* Alpha, int32 AlphaOutput, int32 X, int32 Y)
	{
		UMaterialExpressionLinearInterpolate* Node = AddExpression<UMaterialExpressionLinearInterpolate>(Material, X, Y);
		Node->ConstA = A;
		Node->ConstB = B;
		Node->Alpha.Connect(AlphaOutput, Alpha);
		return Node;
	}

	UMaterialExpressionPower* AddPowerConst(UMaterial* Material, UMaterialExpression* Base, float Exponent, int32 X, int32 Y)
	{
		UMaterialExpressionPower* Node = AddExpression<UMaterialExpressionPower>(Material, X, Y);
		Node->Base.Connect(0, Base);
		Node->ConstExponent = Exponent;
		return Node;
	}

	UMaterialExpressionSaturate* AddSaturate(UMaterial* Material, UMaterialExpression* Input, int32 X, int32 Y)
	{
		UMaterialExpressionSaturate* Node = AddExpression<UMaterialExpressionSaturate>(Material, X, Y);
		Node->Input.Connect(0, Input);
		return Node;
	}

	UMaterialExpressionComponentMask* AddMask(UMaterial* Material, UMaterialExpression* Input, bool R, bool G, bool B, bool A, int32 X, int32 Y)
	{
		UMaterialExpressionComponentMask* Node = AddExpression<UMaterialExpressionComponentMask>(Material, X, Y);
		Node->Input.Connect(0, Input);
		Node->R = R;
		Node->G = G;
		Node->B = B;
		Node->A = A;
		return Node;
	}

	/** Soft round dot from sprite/quad UVs: saturate(1 - 2*|uv - 0.5|)^Exponent. */
	UMaterialExpression* AddSoftDot(UMaterial* Material, float Exponent, int32 X, int32 Y)
	{
		UMaterialExpressionTextureCoordinate* UV = AddExpression<UMaterialExpressionTextureCoordinate>(Material, X, Y);
		UMaterialExpressionConstant3Vector* Half = AddExpression<UMaterialExpressionConstant3Vector>(Material, X, Y + 100);
		Half->Constant = FLinearColor(0.5f, 0.5f, 0.0f);
		UMaterialExpression* HalfUV = AddMask(Material, Half, true, true, false, false, X + 150, Y + 100);
		UMaterialExpressionDistance* Dist = AddExpression<UMaterialExpressionDistance>(Material, X + 300, Y);
		Dist->A.Connect(0, UV);
		Dist->B.Connect(0, HalfUV);
		UMaterialExpressionOneMinus* Inv = AddExpression<UMaterialExpressionOneMinus>(Material, X + 600, Y);
		Inv->Input.Connect(0, AddMultiplyConst(Material, Dist, 2.0f, X + 450, Y));
		return AddPowerConst(Material, AddSaturate(Material, Inv, X + 750, Y), Exponent, X + 900, Y);
	}

	// ------------------------------------------------------------------ M_GlobeParticle

	/**
	 * Additive soft sprite for NS_GlobeSurface. Niagara supplies colour (Particle Color), a random seed,
	 * normalised age, and Dynamic Parameter 0 = (ParticleIntensity, GlobalEnergy, BackSideVisibility, LandMaskStrength).
	 * The Niagara component sits at the sphere centre with the Earth's rotation, so Object Position is the
	 * sphere centre and the local-space direction indexes the equirectangular land mask.
	 */
	void BuildParticleGraph(UMaterial* Material, UTexture* LandMaskTexture)
	{
		using namespace GlobeVFX;

		UMaterialExpressionScalarParameter* BaseBrightness = AddScalarParam(Material, Params::BaseBrightness, 4.0f, 0.0f, 20.0f, -1800, -600);
		UMaterialExpressionScalarParameter* TwinkleAmount = AddScalarParam(Material, Params::TwinkleAmount, 0.6f, 0.0f, 1.0f, -1800, -500);
		UMaterialExpressionScalarParameter* OceanBrightness = AddScalarParam(Material, Params::OceanBrightness, 0.2f, 0.0f, 1.0f, -1800, 900);

		UMaterialExpressionDynamicParameter* Dyn = AddExpression<UMaterialExpressionDynamicParameter>(Material, -1800, -250);
		Dyn->DefaultValue = FLinearColor(1.0f, 0.3f, 1.0f, 0.0f); // Preview values when not driven by Niagara.

		// Sprite shape.
		UMaterialExpression* Dot = AddSoftDot(Material, 2.2f, -1800, -900);

		// Energy: brighter and faster twinkle, never static (Energy 0 still twinkles at the idle rate).
		UMaterialExpression* EnergyBright = AddLerpConst(Material, 0.6f, 1.5f, Dyn, 1, -1500, -300);
		UMaterialExpression* TwinkleRate = AddLerpConst(Material, 1.5f, 4.0f, Dyn, 1, -1500, -200);

		UMaterialExpressionParticleRandom* Rand = AddExpression<UMaterialExpressionParticleRandom>(Material, -1800, -50);
		UMaterialExpressionTime* Time = AddExpression<UMaterialExpressionTime>(Material, -1800, 50);
		UMaterialExpression* RateJitter = AddAddConst(Material, AddMultiplyConst(Material, Rand, 0.8f, -1500, -50), 0.6f, -1350, -50);
		UMaterialExpression* Phase = AddAdd(Material,
			AddMultiply(Material, AddMultiply(Material, Time, TwinkleRate, -1350, 50), RateJitter, -1200, 50),
			AddMultiplyConst(Material, Rand, 62.83f, -1350, 150), -1050, 50);
		UMaterialExpressionSine* Twinkle = AddExpression<UMaterialExpressionSine>(Material, -900, 50);
		Twinkle->Input.Connect(0, Phase);
		Twinkle->Period = 6.2831853f;
		UMaterialExpression* Twinkle01 = AddAddConst(Material, AddMultiplyConst(Material, Twinkle, 0.5f, -750, 50), 0.5f, -600, 50);
		UMaterialExpressionLinearInterpolate* Flicker = AddExpression<UMaterialExpressionLinearInterpolate>(Material, -450, 50);
		Flicker->ConstA = 1.0f;
		Flicker->B.Connect(0, Twinkle01);
		Flicker->Alpha.Connect(0, TwinkleAmount);

		// Lifetime fade in/out: sin(pi * age), so respawning points never pop.
		UMaterialExpressionParticleRelativeTime* Age = AddExpression<UMaterialExpressionParticleRelativeTime>(Material, -1800, 250);
		UMaterialExpressionSine* LifeFade = AddExpression<UMaterialExpressionSine>(Material, -1500, 250);
		LifeFade->Input.Connect(0, Age);
		LifeFade->Period = 2.0f;

		// Direction from the sphere centre (world) and front/back facing relative to the camera.
		UMaterialExpressionParticlePositionWS* ParticlePos = AddExpression<UMaterialExpressionParticlePositionWS>(Material, -1800, 400);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -1800, 500);
		UMaterialExpressionCameraPositionWS* CameraPos = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1800, 600);
		UMaterialExpressionNormalize* DirWS = AddExpression<UMaterialExpressionNormalize>(Material, -1400, 400);
		DirWS->VectorInput.Connect(0, AddSubtract(Material, ParticlePos, Center, -1550, 400));
		UMaterialExpressionNormalize* ToCamera = AddExpression<UMaterialExpressionNormalize>(Material, -1400, 550);
		ToCamera->VectorInput.Connect(0, AddSubtract(Material, CameraPos, Center, -1550, 550));
		UMaterialExpressionDotProduct* Facing = AddExpression<UMaterialExpressionDotProduct>(Material, -1250, 450);
		Facing->A.Connect(0, DirWS);
		Facing->B.Connect(0, ToCamera);
		// Visible from the camera = in front of the sphere's horizon: facing > r / d, where r is the point's
		// distance from the centre and d the camera's. (Facing > 0 alone would let points just past the rim through.)
		UMaterialExpressionDistance* PointRadius = AddExpression<UMaterialExpressionDistance>(Material, -1400, 250);
		PointRadius->A.Connect(0, ParticlePos);
		PointRadius->B.Connect(0, Center);
		UMaterialExpressionDistance* CameraDistance = AddExpression<UMaterialExpressionDistance>(Material, -1400, 320);
		CameraDistance->A.Connect(0, CameraPos);
		CameraDistance->B.Connect(0, Center);
		UMaterialExpressionDivide* Horizon = AddExpression<UMaterialExpressionDivide>(Material, -1250, 280);
		Horizon->A.Connect(0, PointRadius);
		Horizon->B.Connect(0, CameraDistance);
		UMaterialExpressionSmoothStep* Front = AddExpression<UMaterialExpressionSmoothStep>(Material, -1100, 450);
		Front->Min.Connect(0, AddAddConst(Material, Horizon, -0.06f, -1150, 250));
		Front->Max.Connect(0, AddAddConst(Material, Horizon, 0.02f, -1150, 320));
		Front->Value.Connect(0, Facing);
		UMaterialExpressionLinearInterpolate* BackFactor = AddExpression<UMaterialExpressionLinearInterpolate>(Material, -950, 450);
		BackFactor->A.Connect(2, Dyn); // BackSideVisibility
		BackFactor->ConstB = 1.0f;
		BackFactor->Alpha.Connect(0, Front);

		// Land mask: local direction -> equirectangular UV (matches the baker: lon = atan2(y, x), lat = asin(z)).
		UMaterialExpressionTransform* DirLocal = AddExpression<UMaterialExpressionTransform>(Material, -1250, 700);
		DirLocal->Input.Connect(0, DirWS);
		DirLocal->TransformSourceType = TRANSFORMSOURCE_World;
		DirLocal->TransformType = TRANSFORM_Local;
		UMaterialExpressionNormalize* DirL = AddExpression<UMaterialExpressionNormalize>(Material, -1100, 700);
		DirL->VectorInput.Connect(0, DirLocal);
		UMaterialExpressionArctangent2* Lon = AddExpression<UMaterialExpressionArctangent2>(Material, -950, 700);
		Lon->Y.Connect(0, AddMask(Material, DirL, false, true, false, false, -1000, 650));
		Lon->X.Connect(0, AddMask(Material, DirL, true, false, false, false, -1000, 750));
		UMaterialExpressionArcsine* Lat = AddExpression<UMaterialExpressionArcsine>(Material, -950, 850);
		Lat->Input.Connect(0, AddMask(Material, DirL, false, false, true, false, -1000, 850));
		UMaterialExpression* U = AddAddConst(Material, AddMultiplyConst(Material, Lon, 1.0f / UE_TWO_PI, -800, 700), 0.5f, -650, 700);
		UMaterialExpressionOneMinus* V = AddExpression<UMaterialExpressionOneMinus>(Material, -650, 850);
		V->Input.Connect(0, AddAddConst(Material, AddMultiplyConst(Material, Lat, 1.0f / UE_PI, -800, 850), 0.5f, -700, 850));
		UMaterialExpressionAppendVector* MaskUV = AddExpression<UMaterialExpressionAppendVector>(Material, -500, 750);
		MaskUV->A.Connect(0, U);
		MaskUV->B.Connect(0, V);

		UMaterialExpressionTextureSampleParameter2D* Mask = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -350, 750);
		Mask->ParameterName = FName(Params::LandMask);
		Mask->Group = ParamGroup;
		Mask->Coordinates.Connect(0, MaskUV);
		Mask->MipValueMode = TMVM_MipLevel; // atan2 seam would otherwise pick a tiny mip along lon = 180.
		Mask->ConstMipValue = 0;
		if (LandMaskTexture)
		{
			Mask->Texture = LandMaskTexture;
			Mask->SamplerType = SAMPLERTYPE_LinearGrayscale;
		}
		else
		{
			Mask->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			Mask->SamplerType = SAMPLERTYPE_Color;
		}

		UMaterialExpressionLinearInterpolate* LandLevel = AddExpression<UMaterialExpressionLinearInterpolate>(Material, -150, 800);
		LandLevel->A.Connect(0, OceanBrightness);
		LandLevel->ConstB = 1.0f;
		LandLevel->Alpha.Connect(1, Mask); // R
		UMaterialExpressionLinearInterpolate* MaskFactor = AddExpression<UMaterialExpressionLinearInterpolate>(Material, 0, 800);
		MaskFactor->ConstA = 1.0f;
		MaskFactor->B.Connect(0, LandLevel);
		MaskFactor->Alpha.Connect(3, Dyn); // LandMaskStrength

		// Emissive = colour * brightness * dot * intensity * energy * twinkle * life * back * land
		UMaterialExpressionParticleColor* Color = AddExpression<UMaterialExpressionParticleColor>(Material, -1800, -750);
		UMaterialExpression* E = AddMultiply(Material, Color, BaseBrightness, -600, -700);
		E = AddMultiply(Material, E, Dot, -450, -650);
		UMaterialExpressionMultiply* WithIntensity = AddExpression<UMaterialExpressionMultiply>(Material, -300, -600);
		WithIntensity->A.Connect(0, E);
		WithIntensity->B.Connect(0, Dyn); // ParticleIntensity (x)
		E = AddMultiply(Material, WithIntensity, EnergyBright, -150, -550);
		E = AddMultiply(Material, E, Flicker, 0, -500);
		E = AddMultiply(Material, E, LifeFade, 150, -450);
		E = AddMultiply(Material, E, BackFactor, 300, -400);
		E = AddMultiply(Material, E, MaskFactor, 450, -350);

		// Centre clearing: Dynamic Parameter 1 = (Amount, Threshold, Softness), from Niagara User params.
		UMaterialExpressionDynamicParameter* Dyn1 = AddExpression<UMaterialExpressionDynamicParameter>(Material, -1250, 1100);
		Dyn1->ParameterIndex = 1;
		Dyn1->DefaultValue = FLinearColor(0.0f, 0.9f, 0.1f, 0.0f); // Off unless driven.
		UMaterialExpression* CenterKeep = AddCenterClear(Material, Facing, { Dyn1, 0 }, { Dyn1, 1 }, { Dyn1, 2 }, -1000, 1100);
		E = AddMultiply(Material, E, CenterKeep, 600, -300);

		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, E);
	}

	// ------------------------------------------------------------------ M_GlobeNetwork

	/**
	 * Additive two-sided material for UGlobeNetworkComponent's generated mesh.
	 * Vertex colour: R = brightness, G = phase seed, B = growth-speed seed, A = 0 for link ribbons / 1 for node quads.
	 * UV: links run U 0..1 along their length with V across the width; nodes use a 0..1 quad.
	 */
	void BuildNetworkGraph(UMaterial* Material)
	{
		using namespace GlobeVFX;

		UMaterialExpressionVectorParameter* Color = AddExpression<UMaterialExpressionVectorParameter>(Material, -1400, -300);
		Color->ParameterName = FName(Params::NetworkColor);
		Color->Group = ParamGroup;
		Color->DefaultValue = FLinearColor(0.1f, 0.6f, 1.0f, 1.0f);
		UMaterialExpressionScalarParameter* Intensity = AddScalarParam(Material, Params::NetworkIntensity, 1.0f, 0.0f, 10.0f, -1400, -150);
		UMaterialExpressionScalarParameter* PulseSpeed = AddScalarParam(Material, Params::PulseSpeed, 0.35f, 0.0f, 3.0f, -1400, 100);

		UMaterialExpressionVertexColor* VC = AddExpression<UMaterialExpressionVertexColor>(Material, -1400, 300);
		UMaterialExpressionTextureCoordinate* UV = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1400, 500);

		// Link ribbon: soft across the width, plus a bright pulse travelling along it.
		UMaterialExpression* Across = AddMask(Material, UV, false, true, false, false, -1200, 500);
		UMaterialExpressionAbs* Centered = AddExpression<UMaterialExpressionAbs>(Material, -900, 500);
		Centered->Input.Connect(0, AddAddConst(Material, AddMultiplyConst(Material, Across, 2.0f, -1100, 500), -1.0f, -1000, 500));
		UMaterialExpressionOneMinus* AcrossFalloff = AddExpression<UMaterialExpressionOneMinus>(Material, -800, 500);
		AcrossFalloff->Input.Connect(0, Centered);
		UMaterialExpression* Width = AddPowerConst(Material, AcrossFalloff, 2.0f, -650, 500);

		UMaterialExpression* Along = AddMask(Material, UV, true, false, false, false, -1200, 650);
		UMaterialExpressionTime* Time = AddExpression<UMaterialExpressionTime>(Material, -1400, 750);
		UMaterialExpression* Travel = AddSubtract(Material, AddAdd(Material, Along, AddMask(Material, VC, false, true, false, false, -1200, 350), -1050, 650),
			AddMultiply(Material, Time, PulseSpeed, -1200, 750), -900, 700);
		UMaterialExpressionFrac* Frac = AddExpression<UMaterialExpressionFrac>(Material, -750, 700);
		Frac->Input.Connect(0, Travel);
		UMaterialExpression* Pulse = AddMultiplyConst(Material, AddPowerConst(Material, Frac, 16.0f, -600, 700), 3.0f, -450, 700);
		// Growth: each link repeatedly draws itself from node A to node B with a bright head, holds, fades,
		// and starts again. Per-link phase (VC.G) and speed (VC.B) keep the web from pulsing in sync.
		UMaterialExpressionScalarParameter* GrowCycle = AddScalarParam(Material, Params::GrowCycle, 6.0f, 0.5f, 30.0f, -1400, 1250);
		UMaterialExpressionScalarParameter* GrowEnabled = AddScalarParam(Material, Params::GrowEnabled, 0.0f, 0.0f, 1.0f, -1400, 1350);
		UMaterialExpressionDivide* TimeOverCycle = AddExpression<UMaterialExpressionDivide>(Material, -1200, 1250);
		TimeOverCycle->A.Connect(0, Time);
		TimeOverCycle->B.Connect(0, GrowCycle);
		UMaterialExpression* SpeedJitter = AddAddConst(Material, AddMultiplyConst(Material, AddMask(Material, VC, false, false, true, false, -1200, 1450), 0.8f, -1050, 1450), 0.6f, -900, 1450);
		UMaterialExpressionFrac* Cycle = AddExpression<UMaterialExpressionFrac>(Material, -750, 1300);
		Cycle->Input.Connect(0, AddAdd(Material, AddMultiply(Material, TimeOverCycle, SpeedJitter, -1050, 1300),
			AddMask(Material, VC, false, true, false, false, -1050, 1200), -900, 1300));
		UMaterialExpression* Head = AddSaturate(Material, AddMultiplyConst(Material, Cycle, 1.0f / 0.55f, -600, 1300), -450, 1300);

		UMaterialExpressionSmoothStep* Edge = AddExpression<UMaterialExpressionSmoothStep>(Material, -300, 1250);
		Edge->Min.Connect(0, AddAddConst(Material, Head, -0.015f, -350, 1200));
		Edge->Max.Connect(0, Head);
		Edge->Value.Connect(0, Along);
		UMaterialExpressionOneMinus* Reveal = AddExpression<UMaterialExpressionOneMinus>(Material, -150, 1250);
		Reveal->Input.Connect(0, Edge);

		UMaterialExpressionSmoothStep* FadeOutStep = AddExpression<UMaterialExpressionSmoothStep>(Material, -450, 1450);
		FadeOutStep->ConstMin = 0.8f;
		FadeOutStep->ConstMax = 1.0f;
		FadeOutStep->Value.Connect(0, Cycle);
		UMaterialExpressionOneMinus* FadeOut = AddExpression<UMaterialExpressionOneMinus>(Material, -300, 1450);
		FadeOut->Input.Connect(0, FadeOutStep);
		UMaterialExpression* Grow = AddMultiply(Material, Reveal, FadeOut, 0, 1300);

		// Bright head while extending.
		UMaterialExpressionAbs* HeadDist = AddExpression<UMaterialExpressionAbs>(Material, -300, 1600);
		HeadDist->Input.Connect(0, AddSubtract(Material, Along, Head, -450, 1600));
		UMaterialExpressionOneMinus* HeadNear = AddExpression<UMaterialExpressionOneMinus>(Material, -50, 1600);
		HeadNear->Input.Connect(0, AddMultiplyConst(Material, HeadDist, 25.0f, -150, 1600));
		UMaterialExpressionSmoothStep* ExtendingStep = AddExpression<UMaterialExpressionSmoothStep>(Material, -450, 1700);
		ExtendingStep->ConstMin = 0.5f;
		ExtendingStep->ConstMax = 0.56f;
		ExtendingStep->Value.Connect(0, Cycle);
		UMaterialExpressionOneMinus* Extending = AddExpression<UMaterialExpressionOneMinus>(Material, -300, 1700);
		Extending->Input.Connect(0, ExtendingStep);
		UMaterialExpression* HeadGlow = AddMultiply(Material,
			AddMultiplyConst(Material, AddPowerConst(Material, AddSaturate(Material, HeadNear, 50, 1600), 3.0f, 200, 1600), 3.0f, 350, 1600),
			Extending, 500, 1650);

		// Growth only when enabled (the component turns it on in a running game; the editor shows the full web).
		UMaterialExpressionLinearInterpolate* GrowFactor = AddExpression<UMaterialExpressionLinearInterpolate>(Material, 200, 1300);
		GrowFactor->ConstA = 1.0f;
		GrowFactor->B.Connect(0, Grow);
		GrowFactor->Alpha.Connect(0, GrowEnabled);
		UMaterialExpression* Body = AddMultiply(Material, AddAddConst(Material, Pulse, 0.3f, -300, 700), GrowFactor, 350, 900);
		UMaterialExpression* Glow = AddMultiply(Material, HeadGlow, GrowEnabled, 650, 1650);
		UMaterialExpression* LinkTerm = AddMultiply(Material, Width, AddAdd(Material, Body, Glow, 500, 900), 650, 800);

		// Node quad: soft dot.
		UMaterialExpression* NodeTerm = AddMultiplyConst(Material, AddSoftDot(Material, 2.5f, -1400, 950), 1.5f, -300, 950);

		UMaterialExpressionLinearInterpolate* Term = AddExpression<UMaterialExpressionLinearInterpolate>(Material, 0, 750);
		Term->A.Connect(0, LinkTerm);
		Term->B.Connect(0, NodeTerm);
		Term->Alpha.Connect(4, VC); // A

		UMaterialExpression* ColorRGB = AddMask(Material, Color, true, true, true, false, -1200, -300);
		UMaterialExpression* E = AddMultiply(Material, ColorRGB, Intensity, -900, -250);
		E = AddMultiply(Material, E, AddMask(Material, VC, true, false, false, false, -1200, 250), -600, -200);
		E = AddMultiply(Material, E, Term, 200, 0);

		// Centre clearing (same rule as the particles); the component sits at the sphere centre.
		UMaterialExpressionWorldPosition* WorldPos = AddExpression<UMaterialExpressionWorldPosition>(Material, -1400, 1900);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -1400, 2000);
		UMaterialExpressionCameraPositionWS* CameraPos = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1400, 2100);
		UMaterialExpressionNormalize* Dir = AddExpression<UMaterialExpressionNormalize>(Material, -1050, 1900);
		Dir->VectorInput.Connect(0, AddSubtract(Material, WorldPos, Center, -1200, 1900));
		UMaterialExpressionNormalize* ToCamera = AddExpression<UMaterialExpressionNormalize>(Material, -1050, 2050);
		ToCamera->VectorInput.Connect(0, AddSubtract(Material, CameraPos, Center, -1200, 2050));
		UMaterialExpressionDotProduct* Facing = AddExpression<UMaterialExpressionDotProduct>(Material, -900, 1950);
		Facing->A.Connect(0, Dir);
		Facing->B.Connect(0, ToCamera);
		UMaterialExpressionScalarParameter* ClearAmount = AddScalarParam(Material, Params::CenterClearAmount, 0.0f, 0.0f, 1.0f, -1000, 2200);
		UMaterialExpressionScalarParameter* ClearThreshold = AddScalarParam(Material, Params::CenterClearThreshold, 0.9f, 0.0f, 1.0f, -1000, 2300);
		UMaterialExpressionScalarParameter* ClearSoftness = AddScalarParam(Material, Params::CenterClearSoftness, 0.1f, 0.0f, 0.5f, -1000, 2400);
		UMaterialExpression* CenterKeep = AddCenterClear(Material, Facing, { ClearAmount, 0 }, { ClearThreshold, 0 }, { ClearSoftness, 0 }, -750, 2000);
		E = AddMultiply(Material, E, CenterKeep, 400, 100);

		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, E);
	}

	// ------------------------------------------------------------------ M_GlobeCloudCore

	/** Sine with Period 1 (sin(2*pi*x)) of A*U + B*T + C, used for seamless waves around the ring (integer A). */
	UMaterialExpression* AddRingWave(UMaterial* Material, UMaterialExpression* U, UMaterialExpression* T, float A, float B, float C, int32 X, int32 Y)
	{
		UMaterialExpression* Arg = AddAddConst(Material,
			AddAdd(Material, AddMultiplyConst(Material, U, A, X, Y), AddMultiplyConst(Material, T, B, X, Y + 60), X + 150, Y), C, X + 300, Y);
		UMaterialExpressionSine* Wave = AddExpression<UMaterialExpressionSine>(Material, X + 450, Y);
		Wave->Input.Connect(0, Arg);
		Wave->Period = 1.0f;
		return Wave;
	}

	/**
	 * Additive two-sided band around the equator (procedural ring mesh, UV.x = angle 0..1, UV.y = across).
	 * A thin luminous waveform line with smooth, uneven peaks runs through soft vapour. Energy raises the peaks,
	 * speeds the motion, brightens the line and expands the band outward (World Position Offset along the
	 * radial vertex normal). At Energy 0 it keeps a quiet, moving idle state.
	 */
	void BuildCloudCoreGraph(UMaterial* Material)
	{
		using namespace GlobeVFX;

		UMaterialExpressionVectorParameter* Color = AddExpression<UMaterialExpressionVectorParameter>(Material, -1800, -400);
		Color->ParameterName = FName(Params::RingColor);
		Color->Group = ParamGroup;
		Color->DefaultValue = FLinearColor(0.2f, 0.75f, 1.0f, 1.0f);
		UMaterialExpressionScalarParameter* Intensity = AddScalarParam(Material, Params::RingIntensity, 1.0f, 0.0f, 5.0f, -1800, -300);
		UMaterialExpressionScalarParameter* Energy = AddScalarParam(Material, Params::RingEnergy, 0.0f, 0.0f, 1.0f, -1800, -200);
		UMaterialExpressionScalarParameter* WaveHeight = AddScalarParam(Material, Params::WaveHeight, 0.22f, 0.0f, 0.5f, -1800, -100);
		UMaterialExpressionScalarParameter* LineWidth = AddScalarParam(Material, Params::LineWidth, 0.035f, 0.005f, 0.2f, -1800, 0);
		UMaterialExpressionScalarParameter* Vapor = AddScalarParam(Material, Params::VaporStrength, 0.5f, 0.0f, 2.0f, -1800, 100);
		UMaterialExpressionScalarParameter* Expand = AddScalarParam(Material, Params::RingExpand, 40.0f, 0.0f, 300.0f, -1800, 200);
		UMaterialExpressionScalarParameter* Speed = AddScalarParam(Material, Params::WaveSpeed, 1.0f, 0.0f, 5.0f, -1800, 300);

		UMaterialExpressionScalarParameter* Spin = AddScalarParam(Material, Params::RingSpin, 0.011f, -0.2f, 0.2f, -1800, 380);
		UMaterialExpressionTextureCoordinate* UV = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1800, 450);
		UMaterialExpressionTime* Time = AddExpression<UMaterialExpressionTime>(Material, -1800, 650);
		// Slow spin: the whole pattern slides around the ring (seamless, since all waves use integer cycles).
		UMaterialExpression* U = AddSubtract(Material, AddMask(Material, UV, true, false, false, false, -1650, 450),
			AddMultiply(Material, Time, Spin, -1650, 380), -1550, 420);
		UMaterialExpression* V = AddMask(Material, UV, false, true, false, false, -1650, 550);

		// Time runs faster with energy; amplitude has a 35% idle floor so the line always breathes.
		UMaterialExpression* T = AddMultiply(Material, AddMultiply(Material, Time, Speed, -1650, 650), AddLerpConst(Material, 1.0f, 1.8f, Energy, 0, -1650, 750), -1500, 650);
		UMaterialExpression* Amp = AddMultiply(Material, WaveHeight, AddLerpConst(Material, 0.35f, 1.0f, Energy, 0, -1650, 850), -1500, 850);

		// Uneven smooth peaks: three seamless harmonics under a slow envelope (integer cycles around the ring).
		UMaterialExpression* S1 = AddRingWave(Material, U, T, 3.0f, 0.11f, 0.0f, -1400, 400);
		UMaterialExpression* S2 = AddRingWave(Material, U, T, 7.0f, -0.17f, 0.2f, -1400, 550);
		UMaterialExpression* S3 = AddRingWave(Material, U, T, 13.0f, 0.29f, 0.33f, -1400, 700);
		UMaterialExpression* Env = AddAddConst(Material, AddMultiplyConst(Material, AddRingWave(Material, U, T, 2.0f, 0.05f, 0.0f, -1400, 850), 0.4f, -900, 850), 0.6f, -750, 850);
		UMaterialExpression* Sum = AddAdd(Material, AddAdd(Material, AddMultiplyConst(Material, S1, 0.55f, -900, 400), AddMultiplyConst(Material, S2, 0.3f, -900, 550), -750, 450),
			AddMultiplyConst(Material, S3, 0.15f, -900, 700), -600, 500);
		UMaterialExpression* Wave = AddMultiply(Material, AddMultiply(Material, Sum, Env, -450, 550), Amp, -300, 600);

		// Distance from the waveform centre line (band centre is V = 0.5).
		UMaterialExpressionAbs* Dist = AddExpression<UMaterialExpressionAbs>(Material, -150, 500);
		Dist->Input.Connect(0, AddSubtract(Material, AddAddConst(Material, V, -0.5f, -450, 450), Wave, -300, 450));
		UMaterialExpressionDivide* LineD = AddExpression<UMaterialExpressionDivide>(Material, 0, 450);
		LineD->A.Connect(0, Dist);
		LineD->B.Connect(0, LineWidth);
		UMaterialExpressionOneMinus* LineInv = AddExpression<UMaterialExpressionOneMinus>(Material, 150, 450);
		LineInv->Input.Connect(0, LineD);
		UMaterialExpression* Line = AddPowerConst(Material, AddSaturate(Material, LineInv, 300, 450), 2.0f, 450, 450);
		UMaterialExpressionDivide* HaloD = AddExpression<UMaterialExpressionDivide>(Material, 0, 600);
		HaloD->A.Connect(0, Dist);
		HaloD->B.Connect(0, AddMultiplyConst(Material, LineWidth, 6.0f, -150, 650));
		UMaterialExpressionOneMinus* HaloInv = AddExpression<UMaterialExpressionOneMinus>(Material, 150, 600);
		HaloInv->Input.Connect(0, HaloD);
		UMaterialExpression* Halo = AddPowerConst(Material, AddSaturate(Material, HaloInv, 300, 600), 2.0f, 450, 600);

		// Soft vapour filling the band, drifting slowly and brighter with energy.
		UMaterialExpressionAbs* Across = AddExpression<UMaterialExpressionAbs>(Material, -1400, 1000);
		Across->Input.Connect(0, AddAddConst(Material, AddMultiplyConst(Material, V, 2.0f, -1550, 1000), -1.0f, -1500, 1000));
		UMaterialExpressionOneMinus* BandInv = AddExpression<UMaterialExpressionOneMinus>(Material, -1250, 1000);
		BandInv->Input.Connect(0, Across);
		UMaterialExpression* Band = AddPowerConst(Material, BandInv, 1.5f, -1100, 1000);
		UMaterialExpression* Mist = AddMultiply(Material, AddRingWave(Material, U, T, 5.0f, 0.07f, 0.1f, -1400, 1100), AddRingWave(Material, U, T, 9.0f, -0.05f, 0.6f, -1400, 1250), -900, 1150);
		UMaterialExpression* MistVar = AddAddConst(Material, AddMultiplyConst(Material, Mist, 0.4f, -750, 1150), 0.6f, -600, 1150);
		UMaterialExpression* VaporTerm = AddMultiply(Material, AddMultiply(Material, AddMultiply(Material, Band, MistVar, -450, 1050), Vapor, -300, 1050),
			AddLerpConst(Material, 0.7f, 1.4f, Energy, 0, -450, 1150), -150, 1100);

		UMaterialExpression* LineBright = AddMultiply(Material, Line, AddLerpConst(Material, 2.5f, 4.5f, Energy, 0, 300, 350), 600, 400);
		UMaterialExpression* Total = AddAdd(Material, AddAdd(Material, LineBright, AddMultiplyConst(Material, Halo, 0.6f, 600, 600), 750, 450), VaporTerm, 900, 600);
		UMaterialExpression* ColorRGB = AddMask(Material, Color, true, true, true, false, -1650, -400);
		UMaterialExpression* E = AddMultiply(Material, AddMultiply(Material, ColorRGB, Intensity, 700, -300), Total, 1050, 0);

		// Centre clearing while the earth is shown: ring sections in the middle of the view (front arc over the
		// globe, back arc behind it) fade; the sides stay. The component sits at the globe centre.
		UMaterialExpressionWorldPosition* WorldPos = AddExpression<UMaterialExpressionWorldPosition>(Material, 700, 1300);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, 700, 1400);
		UMaterialExpressionCameraPositionWS* CameraPos = AddExpression<UMaterialExpressionCameraPositionWS>(Material, 700, 1500);
		UMaterialExpressionNormalize* Dir = AddExpression<UMaterialExpressionNormalize>(Material, 1000, 1300);
		Dir->VectorInput.Connect(0, AddSubtract(Material, WorldPos, Center, 850, 1300));
		UMaterialExpressionNormalize* ToCamera = AddExpression<UMaterialExpressionNormalize>(Material, 1000, 1450);
		ToCamera->VectorInput.Connect(0, AddSubtract(Material, CameraPos, Center, 850, 1450));
		UMaterialExpressionDotProduct* Facing = AddExpression<UMaterialExpressionDotProduct>(Material, 1150, 1350);
		Facing->A.Connect(0, Dir);
		Facing->B.Connect(0, ToCamera);
		UMaterialExpressionScalarParameter* ClearAmount = AddScalarParam(Material, Params::CenterClearAmount, 0.0f, 0.0f, 1.0f, 1000, 1600);
		UMaterialExpressionScalarParameter* ClearThreshold = AddScalarParam(Material, Params::CenterClearThreshold, 0.8f, 0.0f, 1.0f, 1000, 1700);
		UMaterialExpressionScalarParameter* ClearSoftness = AddScalarParam(Material, Params::CenterClearSoftness, 0.15f, 0.0f, 0.5f, 1000, 1800);
		UMaterialExpression* CenterKeep = AddCenterClear(Material, Facing, { ClearAmount, 0 }, { ClearThreshold, 0 }, { ClearSoftness, 0 }, 1300, 1400);
		E = AddMultiply(Material, E, CenterKeep, 1300, 0);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, E);

		// The band breathes outward with energy (vertex normals point away from the ring centre).
		UMaterialExpressionVertexNormalWS* Normal = AddExpression<UMaterialExpressionVertexNormalWS>(Material, 700, 900);
		Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0,
			AddMultiply(Material, Normal, AddMultiply(Material, Expand, Energy, 700, 1000), 900, 950));
	}

	// ------------------------------------------------------------------ M_GlobeMist

	/**
	 * Soft additive mist sprite for NS_GlobeCloudWave. Particle Color = GlowColor, Dynamic Parameter 0 =
	 * (RingIntensity, AudioEnergy). Depth Fade softens it where it meets the globe (soft particles); lifetime
	 * fade in/out means puffs never pop.
	 */
	void BuildMistGraph(UMaterial* Material)
	{
		using namespace GlobeVFX;

		UMaterialExpressionScalarParameter* Brightness = AddScalarParam(Material, Params::MistBrightness, 0.5f, 0.0f, 5.0f, -1200, -300);
		UMaterialExpressionDynamicParameter* Dyn = AddExpression<UMaterialExpressionDynamicParameter>(Material, -1200, -150);
		Dyn->DefaultValue = FLinearColor(1.0f, 0.0f, 0.0f, 0.0f);

		UMaterialExpression* Puff = AddSoftDot(Material, 1.6f, -1200, 100);
		UMaterialExpressionParticleRelativeTime* Age = AddExpression<UMaterialExpressionParticleRelativeTime>(Material, -1200, 300);
		UMaterialExpressionSine* LifeFade = AddExpression<UMaterialExpressionSine>(Material, -1050, 300);
		LifeFade->Input.Connect(0, Age);
		LifeFade->Period = 2.0f; // sin(pi * age)
		UMaterialExpressionParticleRandom* Rand = AddExpression<UMaterialExpressionParticleRandom>(Material, -1200, 400);
		UMaterialExpression* Variety = AddAddConst(Material, AddMultiplyConst(Material, Rand, 0.5f, -1050, 400), 0.5f, -900, 400);

		UMaterialExpressionDepthFade* Soft = AddExpression<UMaterialExpressionDepthFade>(Material, -900, 500);
		Soft->FadeDistanceDefault = 60.0f;

		UMaterialExpressionParticleColor* Color = AddExpression<UMaterialExpressionParticleColor>(Material, -1200, -450);
		UMaterialExpression* E = AddMultiply(Material, Color, Brightness, -900, -400);
		UMaterialExpressionMultiply* WithIntensity = AddExpression<UMaterialExpressionMultiply>(Material, -750, -350);
		WithIntensity->A.Connect(0, E);
		WithIntensity->B.Connect(0, Dyn); // RingIntensity
		E = AddMultiply(Material, WithIntensity, AddLerpConst(Material, 0.7f, 1.6f, Dyn, 1, -900, -150), -600, -300);
		E = AddMultiply(Material, E, Puff, -450, -250);
		E = AddMultiply(Material, E, LifeFade, -300, -200);
		E = AddMultiply(Material, E, Variety, -150, -150);
		E = AddMultiply(Material, E, Soft, 0, -100);

		// Centre clearing while the earth is shown: Dynamic Parameter 0 z = amount, w = threshold.
		UMaterialExpressionParticlePositionWS* ParticlePos = AddExpression<UMaterialExpressionParticlePositionWS>(Material, -1200, 700);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -1200, 800);
		UMaterialExpressionCameraPositionWS* CameraPos = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1200, 900);
		UMaterialExpressionNormalize* Dir = AddExpression<UMaterialExpressionNormalize>(Material, -900, 700);
		Dir->VectorInput.Connect(0, AddSubtract(Material, ParticlePos, Center, -1050, 700));
		UMaterialExpressionNormalize* ToCamera = AddExpression<UMaterialExpressionNormalize>(Material, -900, 850);
		ToCamera->VectorInput.Connect(0, AddSubtract(Material, CameraPos, Center, -1050, 850));
		UMaterialExpressionDotProduct* Facing = AddExpression<UMaterialExpressionDotProduct>(Material, -750, 750);
		Facing->A.Connect(0, Dir);
		Facing->B.Connect(0, ToCamera);
		UMaterialExpressionScalarParameter* ClearSoftness = AddScalarParam(Material, Params::CenterClearSoftness, 0.15f, 0.0f, 0.5f, -750, 950);
		UMaterialExpression* CenterKeep = AddCenterClear(Material, Facing, { Dyn, 2 }, { Dyn, 3 }, { ClearSoftness, 0 }, -600, 750);
		E = AddMultiply(Material, E, CenterKeep, 150, -50);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, E);
	}

	// ------------------------------------------------------------------ M_GlobeLink

	struct FCustomIn
	{
		const TCHAR* Name;
		FOut Out;
	};

	UMaterialExpressionCustom* AddCustom(UMaterial* Material, const TCHAR* Description, const FString& Code, ECustomMaterialOutputType Output,
		std::initializer_list<FCustomIn> Inputs, int32 X, int32 Y)
	{
		UMaterialExpressionCustom* Node = AddExpression<UMaterialExpressionCustom>(Material, X, Y);
		Node->Description = Description;
		Node->Code = Code;
		Node->OutputType = Output;
		Node->Inputs.Reset();
		for (const FCustomIn& In : Inputs)
		{
			FCustomInput& Custom = Node->Inputs.AddDefaulted_GetRef();
			Custom.InputName = In.Name;
			Custom.Input.Connect(In.Out.Index, In.Out.Node);
		}
		return Node;
	}

	/**
	 * Additive sprite for NS_GlobeDataLink's three emitters. Dynamic Parameter 0 =
	 * (LinkIntensity, lateral width cm, OccluderRadius cm, mode: 0 stream / 1 filament lanes / 2 node glow).
	 *  - Width: sprites are offset in the camera plane (World Position Offset) by a per-particle random amount, so
	 *    one Bezier curve becomes a stream of particles (mode 0) or three thin filament lanes (mode 1).
	 *  - Occlusion: a ray-sphere test against the globe (centre = the component's position) hides anything
	 *    behind it. This works for the translucent hologram too, where depth testing alone would not.
	 */
	void BuildLinkGraph(UMaterial* Material)
	{
		UMaterialExpressionDynamicParameter* Dyn = AddExpression<UMaterialExpressionDynamicParameter>(Material, -1600, -200);
		Dyn->DefaultValue = FLinearColor(1.0f, 0.0f, 0.0f, 0.0f);
		UMaterialExpressionParticleRandom* Rand = AddExpression<UMaterialExpressionParticleRandom>(Material, -1600, 100);
		UMaterialExpressionTime* Time = AddExpression<UMaterialExpressionTime>(Material, -1600, 200);
		UMaterialExpressionParticleRelativeTime* Age = AddExpression<UMaterialExpressionParticleRelativeTime>(Material, -1600, 300);

		// Camera right / up in world space for the lateral offset.
		UMaterialExpressionConstant3Vector* RightV = AddExpression<UMaterialExpressionConstant3Vector>(Material, -1600, 450);
		RightV->Constant = FLinearColor(1.0f, 0.0f, 0.0f);
		UMaterialExpressionConstant3Vector* UpV = AddExpression<UMaterialExpressionConstant3Vector>(Material, -1600, 550);
		UpV->Constant = FLinearColor(0.0f, 1.0f, 0.0f);
		UMaterialExpressionTransform* CamRight = AddExpression<UMaterialExpressionTransform>(Material, -1400, 450);
		CamRight->Input.Connect(0, RightV);
		CamRight->TransformSourceType = TRANSFORMSOURCE_View;
		CamRight->TransformType = TRANSFORM_World;
		UMaterialExpressionTransform* CamUp = AddExpression<UMaterialExpressionTransform>(Material, -1400, 550);
		CamUp->Input.Connect(0, UpV);
		CamUp->TransformSourceType = TRANSFORMSOURCE_View;
		CamUp->TransformType = TRANSFORM_World;

		const FString OffsetCode = TEXT(
			"float r2 = frac(Rand * 91.73 + 0.37);\n"
			"float2 o = float2(0, 0);\n"
			"if (Mode > 3.5) {\n"
			"  // Card loop: the path is exact (Niagara); only a little jitter around it.\n"
			"  float a = Rand * 6.2831853; o = float2(cos(a), sin(a)) * sqrt(r2);\n"
			"  return (CamRight * o.x + CamUp * o.y) * Width;\n"
			"}\n"
			"if (Mode > 2.5) {\n"
			"  // Country swirl: a ring around the core that turns as the particles travel (two turns along the path).\n"
			"  float a = Rand * 6.2831853 + Age * 12.566371 + Time * 1.5;\n"
			"  o = float2(cos(a), sin(a)) * lerp(0.7, 1.0, r2);\n"
			"}\n"
			"else if (Mode > 1.5) { o = float2(0, 0); }\n"
			"else if (Mode > 0.5) { o = float2(floor(Rand * 3.0) - 1.0, 0.0); }\n"
			"else {\n"
			"  float a = Rand * 6.2831853; float rr = sqrt(r2);\n"
			"  o = float2(cos(a), sin(a)) * rr + 0.15 * float2(sin(Time * 1.3 + Rand * 20.0), cos(Time * 1.1 + r2 * 20.0));\n"
			"}\n"
			"float taper = Mode > 0.5 ? sin(3.14159265 * saturate(Age)) : lerp(0.6, 1.0, sin(3.14159265 * saturate(Age)));\n"
			"return (CamRight * o.x + CamUp * o.y) * Width * taper;");
		UMaterialExpressionCustom* Offset = AddCustom(Material, TEXT("Link lateral offset"), OffsetCode, CMOT_Float3,
			{ { TEXT("Rand"), { Rand, 0 } }, { TEXT("Width"), { Dyn, 1 } }, { TEXT("Mode"), { Dyn, 3 } }, { TEXT("Time"), { Time, 0 } },
			  { TEXT("CamRight"), { CamRight, 0 } }, { TEXT("CamUp"), { CamUp, 0 } }, { TEXT("Age"), { Age, 0 } } }, -1100, 350);
		Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0, Offset);

		// Globe occlusion (ray from the camera to this pixel against the globe sphere).
		UMaterialExpressionWorldPosition* WorldPos = AddExpression<UMaterialExpressionWorldPosition>(Material, -1600, 700);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -1600, 800);
		UMaterialExpressionCameraPositionWS* CameraPos = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1600, 900);
		UMaterialExpression* Rel = AddSubtract(Material, WorldPos, Center, -1400, 700);
		UMaterialExpression* CamRel = AddSubtract(Material, CameraPos, Center, -1400, 850);
		const FString OcclusionCode = TEXT(
			"float3 d = Rel - CamRel; float L = length(d); d = d / max(L, 0.001);\n"
			"float b = dot(CamRel, d); float c = dot(CamRel, CamRel) - R * R;\n"
			"float disc = b * b - c;\n"
			"if (disc <= 0.0 || R <= 0.0) return 1.0;\n"
			"float t0 = -b - sqrt(disc);\n"
			"float behind = saturate((L - t0) / max(R * 0.04, 1.0));\n"
			"float edge = saturate(disc / max(R * R * 0.015, 1.0));\n"
			"return 1.0 - behind * edge * step(0.0, t0);");
		UMaterialExpressionCustom* Visible = AddCustom(Material, TEXT("Globe occlusion"), OcclusionCode, CMOT_Float1,
			{ { TEXT("Rel"), { Rel, 0 } }, { TEXT("CamRel"), { CamRel, 0 } }, { TEXT("R"), { Dyn, 2 } } }, -1100, 750);

		// Brightness per mode: stream fades in/out along the path, filaments are faint, nodes pulse.
		const FString BrightCode = TEXT(
			"float life = sin(3.14159265 * saturate(Age));\n"
			"if (Mode > 3.5) return 1.1 * sqrt(life) * (0.75 + 0.25 * sin(Time * 7.0 + Rand * 31.0));\n"
			"if (Mode > 2.5) return 0.9 * sqrt(life) * (0.7 + 0.3 * sin(Time * 6.0 + Rand * 40.0));\n"
			"if (Mode > 1.5) return 2.5 * life * (0.8 + 0.2 * sin(Time * 3.0));\n"
			"if (Mode > 0.5) return 0.35 * sqrt(life);\n"
			"return sqrt(life);");
		UMaterialExpressionCustom* Bright = AddCustom(Material, TEXT("Link brightness"), BrightCode, CMOT_Float1,
			{ { TEXT("Mode"), { Dyn, 3 } }, { TEXT("Age"), { Age, 0 } }, { TEXT("Time"), { Time, 0 } }, { TEXT("Rand"), { Rand, 0 } } }, -1100, 1000);

		UMaterialExpression* Dot = AddSoftDot(Material, 2.0f, -1600, -500);
		UMaterialExpressionParticleColor* Color = AddExpression<UMaterialExpressionParticleColor>(Material, -1600, -650);
		UMaterialExpressionMultiply* E = AddExpression<UMaterialExpressionMultiply>(Material, -700, -500);
		E->A.Connect(0, Color);
		E->B.Connect(0, Dyn); // LinkIntensity (x)
		UMaterialExpression* Out = AddMultiply(Material, AddMultiply(Material, AddMultiply(Material, E, Dot, -550, -450), Bright, -400, -400), Visible, -250, -350);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Out);
	}

	// ------------------------------------------------------------------ M_GlobePrismRibbon / M_GlobePrismSprite

	/** Ray from the camera to this pixel against the globe sphere: 1 in front, BackSide where the globe covers it.
	 *  Same test as M_GlobeLink; it also works while the earth is translucent (crossfade) and for the hologram.
	 *  Front: visibility on the camera's side of the globe (0 = prisms only show behind it, i.e. around its edge). */
	const TCHAR* PrismOcclusionCode = TEXT(
		"float facing = dot(normalize(Rel), normalize(CamRel));\n"
		"float frontFade = lerp(1.0, Front, smoothstep(-0.05, 0.25, facing));\n"
		"float3 d = Rel - CamRel; float L = length(d); d = d / max(L, 0.001);\n"
		"float b = dot(CamRel, d); float c = dot(CamRel, CamRel) - R * R;\n"
		"float disc = b * b - c;\n"
		"float covered = 0.0;\n"
		"if (disc > 0.0 && R > 0.0) {\n"
		"  float t0 = -b - sqrt(disc);\n"
		"  covered = saturate((L - t0) / max(R * 0.03, 1.0)) * saturate(disc / max(R * R * 0.01, 1.0)) * step(0.0, t0);\n"
		"}\n"
		"return lerp(1.0, Back, covered) * frontFade;");

	/** Shared inputs: Dynamic Parameters 0 and 2, the visibility factor, the emissive scale and the globe-relative
	 *  positions of the tested point and the camera (for the flare's rim test). */
	struct FPrismCommon
	{
		UMaterialExpressionDynamicParameter* Dyn0;
		UMaterialExpressionDynamicParameter* Dyn2; // (FrontVisibility, FlareRimBoost, -, -)
		UMaterialExpression* Rel;
		UMaterialExpression* CamRel;
		UMaterialExpression* Visible;
		UMaterialExpressionScalarParameter* Emissive;
		UMaterialExpressionParticleColor* Color;
		UMaterialExpressionTime* Time;
	};

	/** bOccludeAtParticle: test the particle centre instead of each pixel, so a sprite (lens flare) hides as a whole. */
	FPrismCommon AddPrismCommon(UMaterial* Material, float DefaultEmissive, bool bOccludeAtParticle = false)
	{
		FPrismCommon Out;
		Out.Dyn0 = AddExpression<UMaterialExpressionDynamicParameter>(Material, -1600, -300);
		Out.Dyn0->DefaultValue = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f); // trail look, no occluder (preview)
		Out.Emissive = AddScalarParam(Material, GlobeVFX::Params::PrismEmissive, DefaultEmissive, 0.0f, 20.0f, -1600, -450);
		Out.Color = AddExpression<UMaterialExpressionParticleColor>(Material, -1600, -600);
		Out.Time = AddExpression<UMaterialExpressionTime>(Material, -1600, 0);

		UMaterialExpression* WorldPos = bOccludeAtParticle
			? static_cast<UMaterialExpression*>(AddExpression<UMaterialExpressionParticlePositionWS>(Material, -1600, 700))
			: static_cast<UMaterialExpression*>(AddExpression<UMaterialExpressionWorldPosition>(Material, -1600, 700));
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -1600, 800);
		UMaterialExpressionCameraPositionWS* CameraPos = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1600, 900);
		UMaterialExpression* Rel = AddSubtract(Material, WorldPos, Center, -1400, 700);
		UMaterialExpression* CamRel = AddSubtract(Material, CameraPos, Center, -1400, 850);
		Out.Rel = Rel;
		Out.CamRel = CamRel;
		Out.Dyn2 = AddExpression<UMaterialExpressionDynamicParameter>(Material, -1600, 1000);
		Out.Dyn2->ParameterIndex = 2;
		Out.Dyn2->DefaultValue = FLinearColor(1.0f, 0.0f, 0.0f, 0.0f); // visible everywhere unless driven
		Out.Visible = AddCustom(Material, TEXT("Globe occlusion"), PrismOcclusionCode, CMOT_Float1,
			{ { TEXT("Rel"), { Rel, 0 } }, { TEXT("CamRel"), { CamRel, 0 } }, { TEXT("R"), { Out.Dyn0, 2 } }, { TEXT("Back"), { Out.Dyn0, 3 } },
			  { TEXT("Front"), { Out.Dyn2, 0 } } }, -1100, 750);
		return Out;
	}

	/**
	 * Additive sprite for NS_GlobePrisms (sparkles on the ribbon): bright points (Dyn0.x = 1: tight core + small glow, no
	 * lifetime fade) and fine dust (Dyn0.x = 0: soft dot, fades in/out over its life, gentle twinkle).
	 */
	void BuildPrismSpriteGraph(UMaterial* Material)
	{
		const FPrismCommon Common = AddPrismCommon(Material, 3.0f);
		UMaterialExpressionTextureCoordinate* UV = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1600, 400);
		UMaterialExpressionParticleRelativeTime* Age = AddExpression<UMaterialExpressionParticleRelativeTime>(Material, -1600, 500);
		UMaterialExpressionParticleRandom* Rand = AddExpression<UMaterialExpressionParticleRandom>(Material, -1600, 600);

		const FString Code = TEXT(
			"float2 p = UV * 2.0 - 1.0; float r2 = dot(p, p);\n"
			"float edge = saturate(1.0 - sqrt(r2));\n"
			"float headShape = exp(-r2 * 28.0) * 2.2 + pow(edge, 3.0) * 0.45;\n"
			"float dustShape = pow(edge, 2.2) * sin(3.14159265 * saturate(Age)) * (0.7 + 0.3 * sin(Time * 2.1 + Rand * 40.0));\n"
			"float shape = lerp(dustShape, headShape, Sharp);\n"
			"return Color * Alpha * Intensity * Emissive * shape * Visible;");
		UMaterialExpressionCustom* E = AddCustom(Material, TEXT("Prism sprite"), Code, CMOT_Float3,
			{ { TEXT("UV"), { UV, 0 } }, { TEXT("Age"), { Age, 0 } }, { TEXT("Rand"), { Rand, 0 } }, { TEXT("Time"), { Common.Time, 0 } },
			  { TEXT("Sharp"), { Common.Dyn0, 0 } }, { TEXT("Intensity"), { Common.Dyn0, 1 } },
			  { TEXT("Color"), { Common.Color, 0 } }, { TEXT("Alpha"), { Common.Color, 4 } }, { TEXT("Emissive"), { Common.Emissive, 0 } },
			  { TEXT("Visible"), { Common.Visible, 0 } } }, -700, 0);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, E);
	}

	/**
	 * Additive two-sided sheet for UGlobePrismRibbonComponent (the PS3-XMB-style ribbon). Every sheet of the mesh
	 * waves along local Z with GlobeVFX::PrismParams::WaveHeightHlsl (same formula as the sparkles, same RibbonClock),
	 * so the sheets cross each other. Shading follows the XMB look: a faint fill, bright glowing folds where a sheet is
	 * seen edge-on (from the exact wave normal), white-hot cores on the strongest folds, soft borders, the last sheet
	 * gold. It only shows behind the globe unless FrontVisibility > 0, and parts covered by the globe use
	 * BackSideVisibility (dimmed through the hologram, hidden by the earth).
	 */
	void BuildPrismSheetGraph(UMaterial* Material)
	{
		using namespace GlobeVFX;
		namespace PP = GlobeVFX::PrismParams;
		auto Scalar = [Material](const TCHAR* Name, float Default, float Max, int32 Y) { return AddScalarParam(Material, Name, Default, 0.0f, Max, -1800, Y); };
		UMaterialExpressionScalarParameter* Clock = Scalar(PP::RibbonClock, 0.0f, PP::ClockPeriod, -600);
		UMaterialExpressionScalarParameter* Amp = Scalar(PP::RibbonAmp, 180.0f, 1000.0f, -500);
		UMaterialExpressionScalarParameter* Inner = Scalar(PP::RibbonInner, 1290.0f, 5000.0f, -400);
		UMaterialExpressionScalarParameter* Outer = Scalar(PP::RibbonOuter, 2010.0f, 6000.0f, -300);
		UMaterialExpressionScalarParameter* Ramp = Scalar(PP::PrismRamp, 1.0f, 1.0f, -200);
		UMaterialExpressionScalarParameter* RampMin = Scalar(PP::RampMinScale, 0.15f, 1.0f, -100);
		UMaterialExpressionScalarParameter* Layers = Scalar(PP::RibbonLayers, 5.0f, 8.0f, 0);
		UMaterialExpressionScalarParameter* EdgeGlow = Scalar(PP::RibbonEdgeGlow, 1.8f, 10.0f, 100);
		UMaterialExpressionScalarParameter* Fill = Scalar(PP::RibbonFill, 0.035f, 1.0f, 200);
		UMaterialExpressionScalarParameter* Intensity = Scalar(PP::PrismIntensity, 1.0f, 4.0f, 300);
		UMaterialExpressionScalarParameter* Occluder = Scalar(PP::OccluderRadius, 1150.0f, 5000.0f, 400);
		UMaterialExpressionScalarParameter* Back = Scalar(PP::BackSideVisibility, 0.3f, 1.0f, 500);
		UMaterialExpressionScalarParameter* Front = Scalar(PP::FrontVisibility, 0.0f, 1.0f, 600);
		UMaterialExpressionScalarParameter* Emissive = Scalar(Params::PrismEmissive, 1.3f, 20.0f, 700);
		UMaterialExpressionScalarParameter* Flutter = Scalar(PP::RibbonFlutter, 0.6f, 2.0f, 720);
		UMaterialExpressionScalarParameter* FlutterClock = Scalar(PP::RibbonFlutterClock, 0.0f, PP::ClockPeriod, 730);
		UMaterialExpressionScalarParameter* TrailLag = Scalar(PP::RibbonTrailLag, 0.5f, 50.0f, 740);
		UMaterialExpressionScalarParameter* FlutterLag = Scalar(PP::RibbonTrailFlutterLag, 0.35f, 50.0f, 750);
		UMaterialExpressionScalarParameter* TrailOpacity = Scalar(PP::RibbonTrailOpacity, 0.45f, 1.0f, 760);
		auto Color = [Material](const TCHAR* Name, const FLinearColor& Default, int32 Y)
		{
			UMaterialExpressionVectorParameter* Param = AddExpression<UMaterialExpressionVectorParameter>(Material, -1800, Y);
			Param->ParameterName = FName(Name);
			Param->Group = ParamGroup;
			Param->DefaultValue = Default;
			return Param;
		};
		UMaterialExpressionVectorParameter* Blue = Color(PP::BlueColor, FLinearColor(0.08f, 0.55f, 1.0f, 1.0f), 800);
		UMaterialExpressionVectorParameter* Gold = Color(PP::GoldColor, FLinearColor(1.0f, 0.68f, 0.3f, 1.0f), 950);
		UMaterialExpressionTextureCoordinate* UV = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1800, 1100);
		UMaterialExpressionVertexColor* VC = AddExpression<UMaterialExpressionVertexColor>(Material, -1800, 1200);

		const FString Height = PP::WaveHeightHlsl(TEXT("u"), TEXT("v"), TEXT("Clk"), TEXT("seed"), TEXT("A"), TEXT("Flutter"), TEXT("FClk"));
		// Shared preamble: the ramp shrinks the band toward its middle radius and lowers the waves.
		const FString Pre = TEXT(
			"float u = UV.x; float v = UV.y; float seed = VC.g;\n"
			"// Echo sheets (VC.b > 0) run behind in time on both clocks: the soft trail.\n"
			"float Clk = Clock - VC.b * TrailLag; float FClk = FlutterClock - VC.b * FlutterLag;\n"
			"float s = lerp(RampMin, 1.0, Ramp); float A = Amp * s;\n"
			"float rMid = 0.5 * (Inner + Outer);\n"
			"float r0 = lerp(Inner, Outer, v); float r = rMid + (r0 - rMid) * s;\n"
			"float ang = 6.2831853 * u; float2 rad = float2(cos(ang), sin(ang));\n");

		// --- World Position Offset: lift along local Z, pull radially for the ramp; local -> world.
		const FString OffsetCode = Pre + FString::Printf(TEXT("float h = %s;\nreturn float3(rad * (r - r0), h);"), *Height);
		UMaterialExpressionCustom* LocalOffset = AddCustom(Material, TEXT("Prism sheet wave"), OffsetCode, CMOT_Float3,
			{ { TEXT("UV"), { UV, 0 } }, { TEXT("VC"), { VC, 0 } }, { TEXT("Clock"), { Clock, 0 } }, { TEXT("Amp"), { Amp, 0 } },
			  { TEXT("Inner"), { Inner, 0 } }, { TEXT("Outer"), { Outer, 0 } }, { TEXT("Ramp"), { Ramp, 0 } }, { TEXT("RampMin"), { RampMin, 0 } },
			  { TEXT("Flutter"), { Flutter, 0 } }, { TEXT("FlutterClock"), { FlutterClock, 0 } }, { TEXT("TrailLag"), { TrailLag, 0 } }, { TEXT("FlutterLag"), { FlutterLag, 0 } } }, -1300, -400);
		UMaterialExpressionTransform* WorldOffset = AddExpression<UMaterialExpressionTransform>(Material, -1000, -400);
		WorldOffset->Input.Connect(0, LocalOffset);
		WorldOffset->TransformSourceType = TRANSFORMSOURCE_Local;
		WorldOffset->TransformType = TRANSFORM_World;
		Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0, WorldOffset);

		// --- Exact normal of the displaced sheet (analytic derivatives of the same waves); local -> world.
		const FString NormalCode = Pre + FString::Printf(TEXT(
			"float env = 0.55 + 0.45 * v;\n"
			"float fenv = Flutter * (0.25 + 0.75 * v);\n"
			"float dhdu = A * 6.2831853 * (env * %s + fenv * %s);\n"
			"float dhdv = A * (0.45 * %s + env * %s + Flutter * 0.75 * %s + fenv * %s);\n"
			"float3 Tu = float3(-rad.y, rad.x, 0.0) * (6.2831853 * r) + float3(0.0, 0.0, dhdu);\n"
			"float3 Tv = float3(rad, 0.0) * ((Outer - Inner) * s) + float3(0.0, 0.0, dhdv);\n"
			"return normalize(cross(Tv, Tu));"),
			*PP::WaveSum(TEXT("cos"), TEXT("n"), TEXT("u"), TEXT("v"), TEXT("Clk"), TEXT("seed")),
			*PP::FlutterSum(TEXT("cos"), TEXT("n"), TEXT("u"), TEXT("v"), TEXT("FClk"), TEXT("seed")),
			*PP::WaveSum(TEXT("sin"), TEXT("1"), TEXT("u"), TEXT("v"), TEXT("Clk"), TEXT("seed")),
			*PP::WaveSum(TEXT("cos"), TEXT("c"), TEXT("u"), TEXT("v"), TEXT("Clk"), TEXT("seed")),
			*PP::FlutterSum(TEXT("sin"), TEXT("1"), TEXT("u"), TEXT("v"), TEXT("FClk"), TEXT("seed")),
			*PP::FlutterSum(TEXT("cos"), TEXT("c"), TEXT("u"), TEXT("v"), TEXT("FClk"), TEXT("seed")));
		UMaterialExpressionCustom* LocalNormal = AddCustom(Material, TEXT("Prism sheet normal"), NormalCode, CMOT_Float3,
			{ { TEXT("UV"), { UV, 0 } }, { TEXT("VC"), { VC, 0 } }, { TEXT("Clock"), { Clock, 0 } }, { TEXT("Amp"), { Amp, 0 } },
			  { TEXT("Inner"), { Inner, 0 } }, { TEXT("Outer"), { Outer, 0 } }, { TEXT("Ramp"), { Ramp, 0 } }, { TEXT("RampMin"), { RampMin, 0 } },
			  { TEXT("Flutter"), { Flutter, 0 } }, { TEXT("FlutterClock"), { FlutterClock, 0 } }, { TEXT("TrailLag"), { TrailLag, 0 } }, { TEXT("FlutterLag"), { FlutterLag, 0 } } }, -1300, 0);
		UMaterialExpressionTransform* WorldNormal = AddExpression<UMaterialExpressionTransform>(Material, -1000, 0);
		WorldNormal->Input.Connect(0, LocalNormal);
		WorldNormal->TransformSourceType = TRANSFORMSOURCE_Local;
		WorldNormal->TransformType = TRANSFORM_World;

		// --- Behind-only + globe occlusion (the component sits at the globe centre).
		UMaterialExpressionWorldPosition* WorldPos = AddExpression<UMaterialExpressionWorldPosition>(Material, -1300, 500);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -1300, 600);
		UMaterialExpressionCameraPositionWS* CameraPos = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1300, 700);
		UMaterialExpressionCustom* Visible = AddCustom(Material, TEXT("Globe occlusion"), PrismOcclusionCode, CMOT_Float1,
			{ { TEXT("Rel"), { AddSubtract(Material, WorldPos, Center, -1150, 500), 0 } }, { TEXT("CamRel"), { AddSubtract(Material, CameraPos, Center, -1150, 650), 0 } },
			  { TEXT("R"), { Occluder, 0 } }, { TEXT("Back"), { Back, 0 } }, { TEXT("Front"), { Front, 0 } } }, -1000, 600);

		// --- XMB shading. Two light sources: folds seen edge-on (view dependent, the classic XMB lines) and trail lines
		// that run along each sheet and wander across it with the waves (view independent, so a tilted ribbon seen
		// face-on still shows bright trails). Line paths use whole cycles around the ring and per clock period.
		UMaterialExpressionScalarParameter* LineGlow = Scalar(PP::RibbonLineGlow, 2.0f, 10.0f, 1300);
		UMaterialExpressionScalarParameter* LineWidth = Scalar(PP::RibbonLineWidth, 0.008f, 0.3f, 1400);
		UMaterialExpressionScalarParameter* LineHalo = Scalar(PP::RibbonLineHalo, 0.35f, 2.0f, 1500);
		UMaterialExpressionCameraVectorWS* ToCamera = AddExpression<UMaterialExpressionCameraVectorWS>(Material, -1000, 200);
		const FString ShadeCode = FString::Printf(TEXT(
			"float u = UV.x; float v = UV.y;\n"
			"float Clk = Clock - VC.b * TrailLag;\n"
			"// Echo sheets (the trail) fade out the further behind they run; the real sheet (VC.b = 0) is untouched.\n"
			"float trail = VC.b > 0.001 ? TrailOpacity * pow(1.0 - VC.b, 1.5) : 1.0;\n"
			"float facing = abs(dot(normalize(N), ToCamera));\n"
			"float fold = pow(1.0 - facing, 7.0);\n"
			"float across = smoothstep(0.0, 0.18, v) * (1.0 - smoothstep(0.82, 1.0, v));\n"
			"float streaks = 0.65 + 0.35 * pow(abs(sin(3.14159265 * (v * 5.0 + VC.g * 3.0))), 6.0);\n"
			"float l1 = 0.5 + 0.32 * sin(6.2831853 * (2.0 * u + 23.0 * Clk / %.1f) + VC.g * 6.2831853);\n"
			"float l2 = 0.5 + 0.28 * sin(6.2831853 * (3.0 * u - 31.0 * Clk / %.1f) + VC.g * 11.0 + 1.7);\n"
			"float w = max(LineWidth, 0.001);\n"
			"float lines = exp(-pow((v - l1) / w, 2.0)) + 0.7 * exp(-pow((v - l2) / w, 2.0));\n"
			"float halo = (exp(-pow((v - l1) / (w * 4.0), 2.0)) + 0.7 * exp(-pow((v - l2) / (w * 4.0), 2.0))) * LineHalo;\n"
			"float gold = step(0.999, VC.r) * step(1.5, Layers);\n"
			"float3 col = lerp(Blue.rgb, Gold.rgb, gold);\n"
			"float3 e = col * (Fill + EdgeGlow * fold * streaks + LineGlow * (lines + halo)) + (fold * fold * fold * EdgeGlow + lines * lines * LineGlow) * 0.12;\n"
			"return e * across * trail * Intensity * Emissive * saturate(Ramp * 4.0) * Visible;"), PP::ClockPeriod, PP::ClockPeriod);
		UMaterialExpressionCustom* E = AddCustom(Material, TEXT("Prism sheet shading"), ShadeCode, CMOT_Float3,
			{ { TEXT("UV"), { UV, 0 } }, { TEXT("VC"), { VC, 0 } }, { TEXT("N"), { WorldNormal, 0 } }, { TEXT("ToCamera"), { ToCamera, 0 } },
			  { TEXT("Layers"), { Layers, 0 } }, { TEXT("Blue"), { Blue, 0 } }, { TEXT("Gold"), { Gold, 0 } }, { TEXT("Fill"), { Fill, 0 } },
			  { TEXT("EdgeGlow"), { EdgeGlow, 0 } }, { TEXT("Intensity"), { Intensity, 0 } }, { TEXT("Emissive"), { Emissive, 0 } },
			  { TEXT("Ramp"), { Ramp, 0 } }, { TEXT("Visible"), { Visible, 0 } }, { TEXT("Clock"), { Clock, 0 } },
			  { TEXT("LineGlow"), { LineGlow, 0 } }, { TEXT("LineWidth"), { LineWidth, 0 } }, { TEXT("LineHalo"), { LineHalo, 0 } },
			  { TEXT("TrailLag"), { TrailLag, 0 } }, { TEXT("TrailOpacity"), { TrailOpacity, 0 } } }, -600, 200);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, E);
	}

	// ------------------------------------------------------------------ M_GlobeCityLights

	/**
	 * Additive night-side city lights on a thin sphere shell just above the earth (AGlobeVFXActor::CityLights, which
	 * sits at the earth's centre with the earth's rotation, so local directions index T_GlobeLandMask exactly like
	 * M_GlobeParticle: lon = atan2(y, x), lat = asin(z)).
	 *  - Lights: one procedural lon/lat grid of light sites (evaluated with the 8 neighbouring cells, so lights are
	 *    never clipped to their cell), each a sharp core + soft halo with an irregular outline, sized between Size Min
	 *    and Size Max (a Big Share of them large), coloured between the two colours; a light per occupied cell,
	 *    occupancy shaped by Density and a coarse "region" hash (dense and empty regions), cells squeezed with
	 *    latitude so dots stay round, no lights toward the icy poles. Only where the land mask says land.
	 *  - Blinking: half the lights run their own on/off cycle (CityBlinkSpeed cycles/s, off for CityBlink of it);
	 *    all twinkle slightly.
	 *  - Night side only: fades in across the terminator from SunDirection (world, toward the sun; zero = all night).
	 *  - CityFade = mode blend from the actor (earth mode on, hologram mode off by default).
	 */
	void BuildCityLightsGraph(UMaterial* Material, UTexture* CityMaskTexture)
	{
		using namespace GlobeVFX;
		auto Scalar = [Material](const TCHAR* Name, float Default, float Max, int32 Y) { return AddScalarParam(Material, Name, Default, 0.0f, Max, -1800, Y); };
		auto Color = [Material](const TCHAR* Name, const FLinearColor& Default, int32 Y)
		{
			UMaterialExpressionVectorParameter* Param = AddExpression<UMaterialExpressionVectorParameter>(Material, -1800, Y);
			Param->ParameterName = FName(Name);
			Param->Group = ParamGroup;
			Param->DefaultValue = Default;
			return Param;
		};
		UMaterialExpressionVectorParameter* Sun = Color(Params::SunDirection, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), -700);
		UMaterialExpressionVectorParameter* ColA = Color(Params::CityColor, FLinearColor(1.0f, 0.62f, 0.28f, 1.0f), -550);
		UMaterialExpressionVectorParameter* ColB = Color(Params::CityColorB, FLinearColor(1.0f, 0.88f, 0.65f, 1.0f), -400);
		UMaterialExpressionScalarParameter* Intensity = Scalar(Params::CityIntensity, 3.0f, 20.0f, -250);
		UMaterialExpressionScalarParameter* Density = Scalar(Params::CityDensity, 0.35f, 1.0f, -150);
		UMaterialExpressionScalarParameter* SizeMin = Scalar(Params::CitySizeMin, 0.06f, 0.45f, -60);
		UMaterialExpressionScalarParameter* SizeMax = Scalar(Params::CitySizeMax, 0.3f, 0.45f, -40);
		UMaterialExpressionScalarParameter* BigShare = Scalar(Params::CityBigShare, 0.1f, 1.0f, -20);
		UMaterialExpressionScalarParameter* Halo = Scalar(Params::CityHalo, 0.3f, 2.0f, 0);
		UMaterialExpressionScalarParameter* GridScale = Scalar(Params::CityScale, 1.0f, 4.0f, 50);
		UMaterialExpressionScalarParameter* Blink = Scalar(Params::CityBlink, 0.25f, 1.0f, 150);
		UMaterialExpressionScalarParameter* BlinkSpeed = Scalar(Params::CityBlinkSpeed, 0.35f, 5.0f, 250);
		UMaterialExpressionScalarParameter* Glow = Scalar(Params::CityGlow, 0.0f, 2.0f, 350);
		UMaterialExpressionScalarParameter* NightSoft = Scalar(Params::CityNightSoftness, 0.12f, 1.0f, 450);
		UMaterialExpressionScalarParameter* Fade = Scalar(Params::CityFade, 1.0f, 1.0f, 550);
		UMaterialExpressionScalarParameter* Darkness = Scalar(Params::CityNightDarkness, 0.85f, 1.0f, 600);
		UMaterialExpressionTime* Time = AddExpression<UMaterialExpressionTime>(Material, -1800, 650);

		// Direction from the earth's centre: world (for the sun test) and local (for the land mask).
		UMaterialExpressionWorldPosition* WorldPos = AddExpression<UMaterialExpressionWorldPosition>(Material, -1800, 800);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -1800, 900);
		UMaterialExpressionNormalize* DirWS = AddExpression<UMaterialExpressionNormalize>(Material, -1450, 850);
		DirWS->VectorInput.Connect(0, AddSubtract(Material, WorldPos, Center, -1600, 850));
		UMaterialExpressionTransform* DirLocal = AddExpression<UMaterialExpressionTransform>(Material, -1300, 950);
		DirLocal->Input.Connect(0, DirWS);
		DirLocal->TransformSourceType = TRANSFORMSOURCE_World;
		DirLocal->TransformType = TRANSFORM_Local;
		UMaterialExpressionNormalize* DirL = AddExpression<UMaterialExpressionNormalize>(Material, -1150, 950);
		DirL->VectorInput.Connect(0, DirLocal);

		// Land mask UV (same mapping as the baker / M_GlobeParticle), sampled at mip 0 (atan2 seam).
		UMaterialExpressionCustom* MaskUV = AddCustom(Material, TEXT("Land mask UV"), TEXT(
			"return float2(atan2(D.y, D.x) / 6.2831853 + 0.5, 1.0 - (asin(clamp(D.z, -1.0, 1.0)) / 3.14159265 + 0.5));"),
			CMOT_Float2, { { TEXT("D"), { DirL, 0 } } }, -1000, 1000);
		UMaterialExpressionTextureSampleParameter2D* Mask = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -800, 1000);
		Mask->ParameterName = FName(TEXT("CityMask"));
		Mask->Group = ParamGroup;
		Mask->Coordinates.Connect(0, MaskUV);
		Mask->MipValueMode = TMVM_MipLevel;
		Mask->ConstMipValue = 0;
		if (CityMaskTexture)
		{
			Mask->Texture = CityMaskTexture;
			Mask->SamplerType = SAMPLERTYPE_LinearGrayscale;
		}
		else
		{
			Mask->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			Mask->SamplerType = SAMPLERTYPE_Color;
		}

		const FString Code = TEXT(
			"float lon = atan2(D.y, D.x) / 6.2831853 + 0.5;\n"
			"float latRad = asin(clamp(D.z, -1.0, 1.0));\n"
			"float lat = latRad / 3.14159265 + 0.5;\n"
			"// Night side: 1 away from the sun, fading across the terminator. No sun given = all night.\n"
			"float hasSun = step(0.5, length(Sun.xyz));\n"
			"float sunDot = dot(N, normalize(Sun.xyz + float3(0.0, 0.0, 1e-4)));\n"
			"float night = lerp(1.0, 1.0 - smoothstep(-NightSoft, NightSoft * 0.5, sunDot), hasSun);\n"
			"float land = smoothstep(0.35, 0.65, Mask);\n"
			"// Ice and snow are already excluded by T_GlobeCityMask (the mesh's local Z is not the geographic pole).\n"
			"// Night shade: the shell darkens the whole night side (opacity); the lights are added on top.\n"
			"float shade = saturate(Darkness * night * Fade);\n"
			"float keep = night * land * Fade;\n"
			"if (keep <= 0.0001) return float4(0.0, 0.0, 0.0, shade);\n"
			"// Smooth regional variation (value noise over a 36 x 18 grid, wrapping in longitude): denser and emptier areas\n"
			"// without visible cell edges.\n"
			"float2 rp = float2(lon, lat) * float2(36.0, 18.0);\n"
			"float2 ri = floor(rp); float2 rf = frac(rp); rf = rf * rf * (3.0 - 2.0 * rf);\n"
			"float rx1 = ri.x + 1.0 - 36.0 * step(35.5, ri.x);\n"
			"float ra = frac(sin(dot(float2(ri.x, ri.y), float2(27.619, 57.583))) * 43758.5453);\n"
			"float rb = frac(sin(dot(float2(rx1, ri.y), float2(27.619, 57.583))) * 43758.5453);\n"
			"float rc = frac(sin(dot(float2(ri.x, ri.y + 1.0), float2(27.619, 57.583))) * 43758.5453);\n"
			"float rd = frac(sin(dot(float2(rx1, ri.y + 1.0), float2(27.619, 57.583))) * 43758.5453);\n"
			"float region = lerp(lerp(ra, rb, rf.x), lerp(rc, rd, rf.x), rf.y);\n"
			"float squeeze = max(cos(latRad), 0.2);\n"
			"// One grid of light sites; every pixel looks at its own and the 8 neighbouring cells, so a light can spill over\n"
			"// cell edges and is never cut off (no square clipping). Sizes are in cell units: most lights near Size Min, a\n"
			"// Big Share of them up to Size Max. Each light: sharp core + soft halo, slightly irregular outline.\n"
			"float2 grid = float2(360.0, 180.0) * Scale;\n"
			"float2 p = float2(lon, lat) * grid;\n"
			"float2 base = floor(p);\n"
			"float smin = min(SizeMin, SizeMax); float smax = max(SizeMin, SizeMax);\n"
			"float3 total = float3(0.0, 0.0, 0.0);\n"
			"for (int j = -1; j <= 1; j++)\n"
			"{\n"
			"  for (int i = -1; i <= 1; i++)\n"
			"  {\n"
			"    float2 cell = base + float2(i, j);\n"
			"    float2 key = float2(cell.x - grid.x * floor(cell.x / grid.x), cell.y);\n"
			"    float h1 = frac(sin(dot(key, float2(12.9898, 78.233))) * 43758.5453);\n"
			"    float h2 = frac(sin(dot(key, float2(39.3468, 11.135))) * 24634.6345);\n"
			"    float h3 = frac(sin(dot(key, float2(73.156, 52.235))) * 15731.7431);\n"
			"    float h4 = frac(sin(dot(key, float2(91.731, 23.317))) * 35711.1137);\n"
			"    float present = step(1.0 - saturate(Density * (0.25 + 1.5 * region)), h1);\n"
			"    float big = step(1.0 - BigShare, h4);\n"
			"    float radius = lerp(smin, smax, big * (0.55 + 0.45 * h3) + (1.0 - big) * h3 * 0.3);\n"
			"    radius = min(radius, 0.45 * squeeze + 0.05);\n"
			"    float2 d = (p - (cell + 0.2 + 0.6 * float2(h2, h3))) * float2(squeeze, 1.0);\n"
			"    float wobble = 1.0 + 0.18 * sin(atan2(d.y, d.x) * 3.0 + h1 * 25.0) + 0.08 * sin(atan2(d.y, d.x) * 5.0 + h2 * 17.0);\n"
			"    float r = length(d) * wobble / max(radius, 0.005);\n"
			"    float spot = exp(-r * r * 5.0) + exp(-r * r * 0.9) * Halo;\n"
			"    float blinker = step(0.5, frac(h2 * 13.0));\n"
			"    float cycle = frac(Time * BlinkSpeed * (0.5 + h3) + h1 * 7.0);\n"
			"    float on = lerp(1.0, step(Blink, cycle), blinker);\n"
			"    float twinkle = 0.85 + 0.15 * sin(Time * (3.0 + 5.0 * h2) + h3 * 40.0);\n"
			"    float bright = lerp(0.7, 1.4, big) * (0.6 + 0.4 * h4);\n"
			"    total += lerp(ColA.rgb, ColB.rgb, h2) * spot * present * on * twinkle * bright;\n"
			"  }\n"
			"}\n"
			"float haze = Glow * (0.3 + 0.7 * region) * Density;\n"
			"// Translucent blend gives emissive * opacity + scene * (1 - opacity): divide so the lights still add at full strength.\n"
			"float3 lights = (total + ColA.rgb * haze) * keep * Intensity;\n"
			"float opacity = max(shade, 0.05 * saturate(max(lights.r, max(lights.g, lights.b)) * 50.0));\n"
			"return float4(lights / max(opacity, 0.001), opacity);");
		UMaterialExpressionCustom* E = AddCustom(Material, TEXT("City lights"), Code, CMOT_Float4,
			{ { TEXT("D"), { DirL, 0 } }, { TEXT("N"), { DirWS, 0 } }, { TEXT("Mask"), { Mask, 0 } }, { TEXT("Sun"), { Sun, 0 } },
			  { TEXT("ColA"), { ColA, 0 } }, { TEXT("ColB"), { ColB, 0 } }, { TEXT("Intensity"), { Intensity, 0 } }, { TEXT("Density"), { Density, 0 } },
			  { TEXT("SizeMin"), { SizeMin, 0 } }, { TEXT("SizeMax"), { SizeMax, 0 } }, { TEXT("BigShare"), { BigShare, 0 } }, { TEXT("Halo"), { Halo, 0 } },
			  { TEXT("Scale"), { GridScale, 0 } }, { TEXT("Blink"), { Blink, 0 } }, { TEXT("BlinkSpeed"), { BlinkSpeed, 0 } },
			  { TEXT("Glow"), { Glow, 0 } }, { TEXT("NightSoft"), { NightSoft, 0 } }, { TEXT("Fade"), { Fade, 0 } }, { TEXT("Time"), { Time, 0 } },
			  { TEXT("Darkness"), { Darkness, 0 } } }, -500, 300);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, AddMask(Material, E, true, true, true, false, -300, 300));
		Material->GetEditorOnlyData()->Opacity.Connect(0, AddMask(Material, E, false, false, false, true, -300, 450));
	}

	// ------------------------------------------------------------------ M_GlobeHologram: continents layer

	/**
	 * Adds the continents to the hologram shell (called after BuildGraph): land drawn in LandColor with a brighter
	 * coastline and faint scan lines, added to the shell's emissive and opacity.
	 * Alignment with the real earth: the direction from the shell's centre is taken into the shell's local space,
	 * then into the earth mesh's local frame with LandAxisX / Y / Z (the earth's local axes expressed in the shell's
	 * local space, pushed by AGlobeVFXActor), and looked up in T_GlobeLandMask exactly like the baker.
	 * LandFade = 1 - earth-mode blend (the continents give way to the real earth on S); Fade = the shell's own fade.
	 */
	void AddHologramLandLayer(UMaterial* Material, UTexture* LandMaskTexture)
	{
		using namespace GlobeVFX;
		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		UMaterialExpression* BaseEmissive = EditorData->EmissiveColor.Expression;
		const int32 BaseEmissiveIndex = EditorData->EmissiveColor.OutputIndex;
		UMaterialExpression* BaseOpacity = EditorData->Opacity.Expression;
		const int32 BaseOpacityIndex = EditorData->Opacity.OutputIndex;
		if (!BaseEmissive || !BaseOpacity)
		{
			return;
		}

		auto Vector = [Material](const TCHAR* Name, const FLinearColor& Default, int32 Y)
		{
			UMaterialExpressionVectorParameter* Param = AddExpression<UMaterialExpressionVectorParameter>(Material, -2600, Y);
			Param->ParameterName = FName(Name);
			Param->Group = ParamGroup;
			Param->DefaultValue = Default;
			return Param;
		};
		UMaterialExpressionVectorParameter* AxisX = Vector(Params::LandAxisX, FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), 1400);
		UMaterialExpressionVectorParameter* AxisY = Vector(Params::LandAxisY, FLinearColor(0.0f, 1.0f, 0.0f, 0.0f), 1550);
		UMaterialExpressionVectorParameter* AxisZ = Vector(Params::LandAxisZ, FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), 1700);
		UMaterialExpressionVectorParameter* LandColor = Vector(Params::LandColor, FLinearColor(0.0f, 0.85f, 1.0f, 1.0f), 1850);
		UMaterialExpressionScalarParameter* Fill = AddScalarParam(Material, Params::LandFill, 1.2f, 0.0f, 10.0f, -2600, 2000);
		UMaterialExpressionScalarParameter* Opacity = AddScalarParam(Material, Params::LandOpacity, 0.35f, 0.0f, 1.0f, -2600, 2100);
		UMaterialExpressionScalarParameter* CoastGlow = AddScalarParam(Material, Params::LandCoastGlow, 3.0f, 0.0f, 20.0f, -2600, 2200);
		UMaterialExpressionScalarParameter* LandFade = AddScalarParam(Material, Params::LandFade, 0.0f, 0.0f, 1.0f, -2600, 2300);
		UMaterialExpressionScalarParameter* Fade = AddScalarParam(Material, Params::Fade, 1.0f, 0.0f, 1.0f, -2600, 2400); // same parameter as the shell's
		UMaterialExpressionTime* Time = AddExpression<UMaterialExpressionTime>(Material, -2600, 2500);

		UMaterialExpressionWorldPosition* WorldPos = AddExpression<UMaterialExpressionWorldPosition>(Material, -2600, 1100);
		UMaterialExpressionObjectPositionWS* Center = AddExpression<UMaterialExpressionObjectPositionWS>(Material, -2600, 1200);
		UMaterialExpressionNormalize* DirWS = AddExpression<UMaterialExpressionNormalize>(Material, -2300, 1150);
		DirWS->VectorInput.Connect(0, AddSubtract(Material, WorldPos, Center, -2450, 1150));
		UMaterialExpressionTransform* DirShell = AddExpression<UMaterialExpressionTransform>(Material, -2150, 1150);
		DirShell->Input.Connect(0, DirWS);
		DirShell->TransformSourceType = TRANSFORMSOURCE_World;
		DirShell->TransformType = TRANSFORM_Local;
		UMaterialExpressionCustom* EarthDir = AddCustom(Material, TEXT("Shell -> earth frame"), TEXT(
			"float3 d = normalize(D);\n"
			"return normalize(float3(dot(d, AX.xyz), dot(d, AY.xyz), dot(d, AZ.xyz)));"),
			CMOT_Float3, { { TEXT("D"), { DirShell, 0 } }, { TEXT("AX"), { AxisX, 0 } }, { TEXT("AY"), { AxisY, 0 } }, { TEXT("AZ"), { AxisZ, 0 } } }, -1950, 1300);
		UMaterialExpressionCustom* MaskUV = AddCustom(Material, TEXT("Land mask UV"), TEXT(
			"return float2(atan2(D.y, D.x) / 6.2831853 + 0.5, 1.0 - (asin(clamp(D.z, -1.0, 1.0)) / 3.14159265 + 0.5));"),
			CMOT_Float2, { { TEXT("D"), { EarthDir, 0 } } }, -1750, 1300);
		UMaterialExpressionTextureSampleParameter2D* Mask = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -1550, 1300);
		Mask->ParameterName = FName(Params::LandMask);
		Mask->Group = ParamGroup;
		Mask->Coordinates.Connect(0, MaskUV);
		Mask->MipValueMode = TMVM_MipLevel; // atan2 seam would otherwise pick a tiny mip along lon = 180
		Mask->ConstMipValue = 0;
		Mask->Texture = LandMaskTexture ? LandMaskTexture : LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/BlackSquareTexture.BlackSquareTexture"));
		Mask->SamplerType = LandMaskTexture ? SAMPLERTYPE_LinearGrayscale : SAMPLERTYPE_Color;

		UMaterialExpressionCustom* Land = AddCustom(Material, TEXT("Hologram continents"), TEXT(
			"float land = smoothstep(0.3, 0.7, Mask);\n"
			"float coast = pow(saturate(1.0 - abs(Mask * 2.0 - 1.0)), 2.0);\n"
			"float scan = 0.8 + 0.2 * sin(Time * 1.5 + E.z * 140.0);\n"
			"float k = LandFade * Fade;\n"
			"float3 e = Color.rgb * (land * Fill * scan + coast * CoastGlow);\n"
			"float o = saturate(land * Opacity * scan + coast * Opacity);\n"
			"return float4(e * k, o * k);"),
			CMOT_Float4, { { TEXT("Mask"), { Mask, 0 } }, { TEXT("E"), { EarthDir, 0 } }, { TEXT("Time"), { Time, 0 } }, { TEXT("Color"), { LandColor, 0 } },
				{ TEXT("Fill"), { Fill, 0 } }, { TEXT("Opacity"), { Opacity, 0 } }, { TEXT("CoastGlow"), { CoastGlow, 0 } },
				{ TEXT("LandFade"), { LandFade, 0 } }, { TEXT("Fade"), { Fade, 0 } } }, -1300, 1300);

		UMaterialExpressionAdd* Emissive = AddExpression<UMaterialExpressionAdd>(Material, 300, -200);
		Emissive->A.Connect(BaseEmissiveIndex, BaseEmissive);
		Emissive->B.Connect(0, AddMask(Material, Land, true, true, true, false, 100, 1300));
		UMaterialExpressionAdd* OpacitySum = AddExpression<UMaterialExpressionAdd>(Material, 300, 300);
		OpacitySum->A.Connect(BaseOpacityIndex, BaseOpacity);
		OpacitySum->B.Connect(0, AddMask(Material, Land, false, false, false, true, 100, 1450));
		EditorData->EmissiveColor.Connect(0, Emissive);
		EditorData->Opacity.Connect(0, AddSaturate(Material, OpacitySum, 450, 300));
	}

	// ------------------------------------------------------------------ build / instance plumbing

	/** Creates or rebuilds the material in place (same asset, so references stay valid) with the given setup. */
	UMaterial* RebuildMaterial(const TCHAR* Name, TFunctionRef<void(UMaterial*)> Configure)
	{
		bool bCreated = false;
		UMaterial* Material = GlobeAssetUtils::FindOrCreateAsset<UMaterial>(GlobeVFX::Assets::MaterialFolder, Name, bCreated);
		if (!Material)
		{
			return nullptr;
		}
		if (!bCreated)
		{
			GlobeAssetUtils::CloseEditorsFor(Material);
		}

		Material->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		Material->MaterialDomain = MD_Surface;
		Configure(Material);
		UMaterialEditingLibrary::RecompileMaterial(Material);

		UE_LOG(LogGlobeMaterialGenerator, Display, TEXT("%s %s"), bCreated ? TEXT("Created") : TEXT("Rebuilt"), *Material->GetPathName());
		return Material;
	}

	/** Creates the instance if missing; otherwise only re-points its parent (keeps the user's overrides). */
	UMaterialInstanceConstant* EnsureInstance(const TCHAR* Name, UMaterial* Parent)
	{
		bool bCreated = false;
		UMaterialInstanceConstant* Instance = GlobeAssetUtils::FindOrCreateAsset<UMaterialInstanceConstant>(GlobeVFX::Assets::MaterialFolder, Name, bCreated);
		if (!Instance)
		{
			return nullptr;
		}
		if (bCreated || Instance->Parent != Parent)
		{
			Instance->Modify();
			UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Parent);
		}
		Instance->PostEditChange();
		UE_LOG(LogGlobeMaterialGenerator, Display, TEXT("%s %s"), bCreated ? TEXT("Created") : TEXT("Reused"), *Instance->GetPathName());
		return Instance;
	}
}

/** M_GlobeHologram: the Fresnel-rim shell plus the continents layer (aligned with the earth through LandAxisX/Y/Z). */
static UMaterial* BuildHologramMaterial(UTexture* LandMask)
{
	return RebuildMaterial(GlobeVFX::Assets::ParentMaterialName, [LandMask](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		BuildGraph(Material);
		AddHologramLandLayer(Material, LandMask);
	});
}

bool FGlobeMaterialGenerator::GenerateHologramMaterial()
{
	using namespace GlobeVFX::Assets;
	UTexture* LandMask = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), TextureFolder, LandMaskName, LandMaskName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UE_CLOG(!LandMask, LogGlobeMaterialGenerator, Warning, TEXT("%s not found; the hologram shows no continents. Run AIGlobe.BakeLandMask, then regenerate."), LandMaskName);
	UMaterial* Hologram = BuildHologramMaterial(LandMask);
	UMaterialInstanceConstant* HologramMI = Hologram ? EnsureInstance(MaterialInstanceName, Hologram) : nullptr;
	return HologramMI && GlobeAssetUtils::SaveAsset(Hologram) && GlobeAssetUtils::SaveAsset(HologramMI);
}

bool FGlobeMaterialGenerator::GenerateLinkMaterial()
{
	using namespace GlobeVFX::Assets;
	UMaterial* Link = RebuildMaterial(LinkMaterialName, [](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		BuildLinkGraph(Material);
	});
	UMaterialInstanceConstant* LinkMI = Link ? EnsureInstance(LinkInstanceName, Link) : nullptr;
	return LinkMI && GlobeAssetUtils::SaveAsset(Link) && GlobeAssetUtils::SaveAsset(LinkMI);
}

bool FGlobeMaterialGenerator::Generate()
{
	using namespace GlobeVFX::Assets;

	UTexture* LandMask = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), TextureFolder, LandMaskName, LandMaskName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UE_CLOG(!LandMask, LogGlobeMaterialGenerator, Warning, TEXT("%s not found; M_GlobeParticle uses a white placeholder mask and the hologram shows no continents. Bake the land mask, then regenerate."), LandMaskName);

	// --- Hologram (Phase 1), with the continents layer ---
	UMaterial* Hologram = BuildHologramMaterial(LandMask);

	// --- Surface particle sprite (Phase 2) ---
	UMaterial* Particle = RebuildMaterial(ParticleMaterialName, [LandMask](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		BuildParticleGraph(Material, LandMask);
	});

	// --- Same sprite drawn on top (no depth test): points stay visible over the opaque earth; the far side is
	// culled in the material by the horizon test, not by depth. ---
	UMaterial* ParticleOnTop = RebuildMaterial(ParticleOnTopMaterialName, [LandMask](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		Material->bDisableDepthTest = true;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		BuildParticleGraph(Material, LandMask);
	});

	// --- Network links/nodes (Phase 2, optional layer) ---
	UMaterial* Network = RebuildMaterial(NetworkMaterialName, [](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		BuildNetworkGraph(Material);
	});

	// --- Phase 3 cloud ring: luminous waveform core (band mesh) and mist sprites ---
	UMaterial* CloudCore = RebuildMaterial(CloudCoreMaterialName, [](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		BuildCloudCoreGraph(Material);
	});
	UMaterial* Mist = RebuildMaterial(MistMaterialName, [](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		BuildMistGraph(Material);
	});

	// --- Four-link data display: stream / filament / node sprites of NS_GlobeDataLink ---
	UMaterial* Link = RebuildMaterial(LinkMaterialName, [](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		BuildLinkGraph(Material);
	});

	if (!Hologram || !Particle || !ParticleOnTop || !Network || !CloudCore || !Mist || !Link)
	{
		return false;
	}
	UMaterialInstanceConstant* CloudCoreMI = EnsureInstance(CloudCoreInstanceName, CloudCore);
	UMaterialInstanceConstant* MistMI = EnsureInstance(MistInstanceName, Mist);
	UMaterialInstanceConstant* LinkMI = EnsureInstance(LinkInstanceName, Link);
	if (!CloudCoreMI || !MistMI || !LinkMI)
	{
		return false;
	}

	UMaterialInstanceConstant* HologramMI = EnsureInstance(MaterialInstanceName, Hologram);
	UMaterialInstanceConstant* ParticleMI = EnsureInstance(ParticleInstanceName, Particle);
	UMaterialInstanceConstant* ParticleOnTopMI = EnsureInstance(ParticleOnTopInstanceName, ParticleOnTop);
	UMaterialInstanceConstant* NetworkMI = EnsureInstance(NetworkInstanceName, Network);
	if (!HologramMI || !ParticleMI || !ParticleOnTopMI || !NetworkMI)
	{
		return false;
	}

	bool bSaved = true;
	for (UObject* Asset : TArray<UObject*>{ Hologram, HologramMI, Particle, ParticleMI, ParticleOnTop, ParticleOnTopMI, Network, NetworkMI, CloudCore, CloudCoreMI, Mist, MistMI, Link, LinkMI })
	{
		bSaved &= GlobeAssetUtils::SaveAsset(Asset);
	}
	return GeneratePrismMaterials() && GenerateCityLightsMaterial() && bSaved;
}

bool FGlobeMaterialGenerator::GeneratePrismMaterials()
{
	using namespace GlobeVFX::Assets;

	// Additive, unlit, depth-tested: the opaque earth hides what is behind it by depth; the materials' sphere test also
	// covers the translucent hologram and the crossfade (where the earth draws without depth).
	UMaterial* Sheet = RebuildMaterial(PrismSheetMaterialName, [](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		BuildPrismSheetGraph(Material);
	});
	UMaterial* Sprite = RebuildMaterial(PrismSpriteMaterialName, [](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		BuildPrismSpriteGraph(Material);
	});
	if (!Sheet || !Sprite)
	{
		return false;
	}
	UMaterialInstanceConstant* SheetMI = EnsureInstance(PrismSheetInstanceName, Sheet);
	UMaterialInstanceConstant* SpriteMI = EnsureInstance(PrismSpriteInstanceName, Sprite);
	if (!SheetMI || !SpriteMI)
	{
		return false;
	}
	bool bSaved = true;
	for (UObject* Asset : TArray<UObject*>{ Sheet, SheetMI, Sprite, SpriteMI })
	{
		bSaved &= GlobeAssetUtils::SaveAsset(Asset);
	}
	return bSaved;
}

bool FGlobeMaterialGenerator::GenerateCityLightsMaterial()
{
	using namespace GlobeVFX::Assets;
	// T_GlobeCityMask: land without ice and snow, baked from the earth's own textures (once, if missing).
	const FString MaskPath = FString::Printf(TEXT("%s/%s.%s"), TextureFolder, CityMaskName, CityMaskName);
	if (!FPackageName::DoesPackageExist(FString(TextureFolder) / CityMaskName))
	{
		FGlobeLandMaskBaker::BakeCityMask();
	}
	UTexture* CityMask = LoadObject<UTexture>(nullptr, *MaskPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UE_CLOG(!CityMask, LogGlobeMaterialGenerator, Warning, TEXT("%s not found; city lights would cover oceans and ice too. Run AIGlobe.BakeCityMask, then regenerate."), CityMaskName);
	// Translucent (darkens the night side, lights added on top), unlit, front faces only (the far side is behind the earth).
	UMaterial* City = RebuildMaterial(CityLightsMaterialName, [CityMask](UMaterial* Material)
	{
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		BuildCityLightsGraph(Material, CityMask);
	});
	UMaterialInstanceConstant* CityMI = City ? EnsureInstance(CityLightsInstanceName, City) : nullptr;
	return CityMI && GlobeAssetUtils::SaveAsset(City) && GlobeAssetUtils::SaveAsset(CityMI);
}
