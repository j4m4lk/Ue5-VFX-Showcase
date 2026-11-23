// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeLandMaskBaker.h"
#include "Globe/GlobeMaterialGenerator.h"
#include "Globe/GlobeNiagaraGenerator.h"
#include "Globe/GlobeWidgetGenerator.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, VFXEditor);

// Editor console commands (Output Log > Cmd). All are idempotent: existing assets are updated in place.

static FAutoConsoleCommand GGenerateGlobeMaterialsCommand(
	TEXT("AIGlobe.GenerateMaterials"),
	TEXT("Creates or updates the hologram, particle and network materials in /Game/AIGlobe/Materials."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobeMaterialGenerator::Generate();
	}));

static FAutoConsoleCommand GBakeGlobeLandMaskCommand(
	TEXT("AIGlobe.BakeLandMask"),
	TEXT("Bakes /Game/AIGlobe/Textures/T_GlobeLandMask from the earth_globe mesh textures. Run AIGlobe.GenerateMaterials afterwards."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobeLandMaskBaker::Bake();
	}));

static FAutoConsoleCommand GGenerateGlobeNiagaraCommand(
	TEXT("AIGlobe.GenerateNiagara"),
	TEXT("Creates or rebuilds /Game/AIGlobe/Niagara/NS_GlobeSurface. Run AIGlobe.GenerateMaterials first."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobeNiagaraGenerator::Generate();
	}));

static FAutoConsoleCommand GGenerateGlobePrismsCommand(
	TEXT("AIGlobe.GeneratePrisms"),
	TEXT("Creates or rebuilds only the prisms: M/MI_GlobePrismSheet, M/MI_GlobePrismSprite and /Game/AIGlobe/Niagara/NS_GlobePrisms."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (FGlobeMaterialGenerator::GeneratePrismMaterials())
		{
			FGlobeNiagaraGenerator::GeneratePrisms();
		}
	}));

static FAutoConsoleCommand GGenerateGlobeCityLightsCommand(
	TEXT("AIGlobe.GenerateCityLights"),
	TEXT("Creates or rebuilds only M/MI_GlobeCityLights (night-side city lights; uses T_GlobeLandMask)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobeMaterialGenerator::GenerateCityLightsMaterial();
	}));

static FAutoConsoleCommand GBakeGlobeCityMaskCommand(
	TEXT("AIGlobe.BakeCityMask"),
	TEXT("Bakes /Game/AIGlobe/Textures/T_GlobeCityMask (land without ice/snow) from the earth_globe textures. Run AIGlobe.GenerateCityLights afterwards."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobeLandMaskBaker::BakeCityMask();
	}));

static FAutoConsoleCommand GGenerateGlobeWidgetsCommand(
	TEXT("AIGlobe.GenerateWidgets"),
	TEXT("Creates the /Game/AIGlobe/UI Widget Blueprints (voice visualizer, stat card) if missing. Arg 'overwrite' rebuilds them (discards your edits to them)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FGlobeWidgetGenerator::GenerateAll(Args.Contains(TEXT("overwrite")));
	}));
