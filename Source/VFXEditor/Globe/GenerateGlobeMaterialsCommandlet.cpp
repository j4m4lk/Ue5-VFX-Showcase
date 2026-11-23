// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GenerateGlobeMaterialsCommandlet.h"

#include "Globe/GlobeLandMaskBaker.h"
#include "Globe/GlobeMaterialGenerator.h"
#include "Globe/GlobeNiagaraGenerator.h"
#include "Globe/GlobeWidgetGenerator.h"
#include "Globe/GlobeVFXParameters.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "ShaderCompiler.h"

UGenerateGlobeMaterialsCommandlet::UGenerateGlobeMaterialsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace
{
	/** Returns the number of shader compile errors, or -1 if the material could not be validated (no RHI). */
	int32 CountCompileErrors(const TCHAR* MaterialName)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), GlobeVFX::Assets::MaterialFolder, MaterialName, MaterialName);
		const UMaterial* Material = LoadObject<UMaterial>(nullptr, *Path);
		const FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIFeatureLevel) : nullptr;
		if (!Resource)
		{
			return -1;
		}
		for (const FString& Error : Resource->GetCompileErrors())
		{
			UE_LOG(LogTemp, Error, TEXT("%s compile error: %s"), MaterialName, *Error);
		}
		UE_LOG(LogTemp, Display, TEXT("%s shader compile finished with %d error(s)."), MaterialName, Resource->GetCompileErrors().Num());
		return Resource->GetCompileErrors().Num();
	}
}

int32 UGenerateGlobeMaterialsCommandlet::Main(const FString& Params)
{
	using namespace GlobeVFX::Assets;

	// -PrismsOnly: just the prisms (materials + NS_GlobePrisms); the other assets are left untouched.
	if (FParse::Param(*Params, TEXT("PrismsOnly")))
	{
		if (!FGlobeMaterialGenerator::GeneratePrismMaterials() || !FGlobeNiagaraGenerator::GeneratePrisms())
		{
			return 1;
		}
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		int32 Errors = 0;
		for (const TCHAR* Name : { PrismSheetMaterialName, PrismSpriteMaterialName })
		{
			Errors += FMath::Max(0, CountCompileErrors(Name));
		}
		return Errors == 0 ? 0 : 1;
	}

	// -HologramOnly: just M_GlobeHologram / MI_GlobeHologram (with the continents layer).
	if (FParse::Param(*Params, TEXT("HologramOnly")))
	{
		if (!FGlobeMaterialGenerator::GenerateHologramMaterial())
		{
			return 1;
		}
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		return CountCompileErrors(ParentMaterialName) > 0 ? 1 : 0;
	}

	// -LinksOnly: just M_GlobeLink / MI_GlobeLink and NS_GlobeDataLink (the data-link streams).
	if (FParse::Param(*Params, TEXT("LinksOnly")))
	{
		if (!FGlobeMaterialGenerator::GenerateLinkMaterial() || !FGlobeNiagaraGenerator::GenerateDataLink())
		{
			return 1;
		}
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		return CountCompileErrors(LinkMaterialName) > 0 ? 1 : 0;
	}

	// -StatCardOnly [-Overwrite]: just WBP_GlobeStatCard.
	if (FParse::Param(*Params, TEXT("StatCardOnly")))
	{
		return FGlobeWidgetGenerator::GenerateStatCardWidget(FParse::Param(*Params, TEXT("Overwrite"))) ? 0 : 1;
	}

	// -WidgetsOnly [-Overwrite]: just the UI Widget Blueprints (existing ones are kept unless -Overwrite).
	if (FParse::Param(*Params, TEXT("WidgetsOnly")))
	{
		return FGlobeWidgetGenerator::GenerateAll(FParse::Param(*Params, TEXT("Overwrite"))) ? 0 : 1;
	}

	// -CityLightsOnly: just M_GlobeCityLights / MI_GlobeCityLights.
	if (FParse::Param(*Params, TEXT("CityLightsOnly")))
	{
		if (FParse::Param(*Params, TEXT("BakeMask")))
		{
			FGlobeLandMaskBaker::BakeCityMask();
		}
		if (!FGlobeMaterialGenerator::GenerateCityLightsMaterial())
		{
			return 1;
		}
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		return CountCompileErrors(CityLightsMaterialName) > 0 ? 1 : 0;
	}

	// 1. Land mask (needed by M_GlobeParticle). Baked when missing or when -BakeMask is passed.
	const FString MaskPackage = FString(TextureFolder) / LandMaskName;
	if (FParse::Param(*Params, TEXT("BakeMask")) || !FPackageName::DoesPackageExist(MaskPackage))
	{
		if (!FGlobeLandMaskBaker::Bake())
		{
			UE_LOG(LogTemp, Warning, TEXT("Land mask bake failed; continuing with the uniform (unmasked) fallback."));
		}
	}

	// 2. Materials.
	if (!FGlobeMaterialGenerator::Generate())
	{
		return 1;
	}

	// 3. Niagara systems.
	if (!FGlobeNiagaraGenerator::Generate())
	{
		return 1;
	}

	// 4. UI Widget Blueprints (kept if they already exist: they hold your edits).
	if (!FGlobeWidgetGenerator::GenerateAll(FParse::Param(*Params, TEXT("Overwrite"))))
	{
		return 1;
	}

	// The process would otherwise exit before shaders compile; wait so graph errors are reported.
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}

	int32 TotalErrors = 0;
	bool bValidated = true;
	for (const TCHAR* Name : { ParentMaterialName, ParticleMaterialName, ParticleOnTopMaterialName, NetworkMaterialName, CloudCoreMaterialName, MistMaterialName, LinkMaterialName, PrismSheetMaterialName, PrismSpriteMaterialName, CityLightsMaterialName })
	{
		const int32 Errors = CountCompileErrors(Name);
		bValidated &= Errors >= 0;
		TotalErrors += FMath::Max(0, Errors);
	}
	if (!bValidated)
	{
		// Commandlets skip rendering init unless -AllowCommandletRendering is passed.
		UE_LOG(LogTemp, Display, TEXT("Assets generated. Material shaders not validated (no RHI); pass -AllowCommandletRendering to validate."));
	}
	return TotalErrors == 0 ? 0 : 1;
}
