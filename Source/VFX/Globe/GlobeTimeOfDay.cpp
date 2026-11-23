// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeTimeOfDay.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Globe/GlobeVFXActor.h"

AGlobeTimeOfDay::AGlobeTimeOfDay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false; // World Partition: always loaded, like the lights it drives.
#endif
}

AGlobeTimeOfDay* AGlobeTimeOfDay::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AGlobeTimeOfDay> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr; // an empty iterator must not be dereferenced
}

void AGlobeTimeOfDay::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Runs on placement and on every Details-panel edit in the editor: the sun follows the slider live.
	ApplyTimeOfDay();
}

void AGlobeTimeOfDay::BeginPlay()
{
	Super::BeginPlay();
	ApplyTimeOfDay();
	SetActorTickEnabled(NeedsTick());
}

void AGlobeTimeOfDay::SetTimeOfDay(float Hours)
{
	TimeOfDay = FMath::Fmod(Hours, 24.0f);
	if (TimeOfDay < 0.0f)
	{
		TimeOfDay += 24.0f;
	}
	ApplyTimeOfDay();
}

void AGlobeTimeOfDay::SetAutoAdvance(bool bOn)
{
	bAutoAdvance = bOn;
	SetActorTickEnabled(NeedsTick());
}

bool AGlobeTimeOfDay::NeedsTick() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && (bAutoAdvance || bSkyNeedsRecapture);
}

void AGlobeTimeOfDay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bAutoAdvance)
	{
		SetTimeOfDay(TimeOfDay + DeltaSeconds * 24.0f / FMath::Max(6.0f, DayLengthMinutes * 60.0f));
	}
	// A sky light without real-time capture is re-captured a few times per second while the sun moves.
	SkyRecaptureTimer -= DeltaSeconds;
	if (bSkyNeedsRecapture && SkyRecaptureTimer <= 0.0f)
	{
		if (USkyLightComponent* Sky = SkyLight ? SkyLight->GetLightComponent() : nullptr)
		{
			Sky->RecaptureSky();
		}
		bSkyNeedsRecapture = false;
		SkyRecaptureTimer = 0.25f;
	}
	if (!NeedsTick())
	{
		SetActorTickEnabled(false);
	}
}

void AGlobeTimeOfDay::ResolveLights()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Sun)
	{
		if (TActorIterator<ADirectionalLight> It(World); It)
		{
			Sun = *It;
		}
	}
	if (!SkyLight)
	{
		if (TActorIterator<ASkyLight> It(World); It)
		{
			SkyLight = *It;
		}
	}
}

void AGlobeTimeOfDay::ApplySkyDomeVisibility()
{
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
		const UStaticMesh* Asset = Mesh ? Mesh->GetStaticMesh() : nullptr;
		if (!Asset || !Asset->GetName().Contains(TEXT("SkySphere")))
		{
			continue;
		}
		It->SetActorHiddenInGame(bHideStaticSkyDome);
#if WITH_EDITOR
		// Editor viewport only, never saved into the level.
		It->SetIsTemporarilyHiddenInEditor(bHideStaticSkyDome);
#endif
	}
}

void AGlobeTimeOfDay::ApplyTimeOfDay()
{
	if (IsTemplate())
	{
		return;
	}
	ResolveLights();
	ApplySkyDomeVisibility();

	// Solar position: hour angle H (0 at noon), latitude L, declination D.
	//   sin(elevation) = sin L sin D + cos L cos D cos H
	//   azimuth (from north toward east) = atan2(-sin H cos D, sin D cos L - cos D cos H sin L)
	const double H = FMath::DegreesToRadians((TimeOfDay - 12.0) * 15.0);
	const double L = FMath::DegreesToRadians(Latitude);
	const double D = FMath::DegreesToRadians(SeasonDeclination);
	const double SinEl = FMath::Sin(L) * FMath::Sin(D) + FMath::Cos(L) * FMath::Cos(D) * FMath::Cos(H);
	const double Elevation = FMath::Asin(FMath::Clamp(SinEl, -1.0, 1.0));
	const double Azimuth = FMath::Atan2(-FMath::Sin(H) * FMath::Cos(D), FMath::Sin(D) * FMath::Cos(L) - FMath::Cos(D) * FMath::Cos(H) * FMath::Sin(L));
	SunElevation = (float)FMath::RadiansToDegrees(Elevation);
	// World: +X = north (turned by NorthYaw), +Y = east, +Z = up. ToSun points from the ground to the sun.
	const FVector Local(FMath::Cos(Elevation) * FMath::Cos(Azimuth), FMath::Cos(Elevation) * FMath::Sin(Azimuth), FMath::Sin(Elevation));
	const FVector ToSun = FRotator(0.0f, NorthYaw, 0.0f).RotateVector(Local);

	// Day factor: full above ~6 degrees, fading through twilight to 0 at -6 degrees.
	const float Day = FMath::SmoothStep(-6.0f, 6.0f, SunElevation);

	if (UDirectionalLightComponent* SunLight = Sun ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent()) : nullptr)
	{
		if (bManageLightSettings)
		{
			SunLight->SetMobility(EComponentMobility::Movable);
			SunLight->SetAtmosphereSunLight(true);
		}
		if (DaySunIntensity <= 0.0f)
		{
			DaySunIntensity = FMath::Max(SunLight->Intensity, 1.0f); // keep the level's own daylight level
		}
		// The light shines along its forward axis, i.e. from the sun toward the ground.
		SunLight->SetWorldRotation((-ToSun).Rotation());
		SunLight->SetIntensity(DaySunIntensity * Day);
		SunLight->SetUseTemperature(bWarmLowSun);
		if (bWarmLowSun)
		{
			SunLight->SetTemperature(FMath::Lerp(3200.0f, 6500.0f, FMath::SmoothStep(0.0f, 25.0f, SunElevation)));
		}
	}

	if (USkyLightComponent* Sky = SkyLight ? SkyLight->GetLightComponent() : nullptr)
	{
		if (bManageLightSettings)
		{
			Sky->SetMobility(EComponentMobility::Movable);
			Sky->SetRealTimeCapture(true); // follows the Sky Atmosphere every frame
		}
		if (DaySkyLightIntensity <= 0.0f)
		{
			DaySkyLightIntensity = FMath::Max(Sky->Intensity, 0.1f);
		}
		Sky->SetIntensity(FMath::Lerp(NightSkyLightIntensity, DaySkyLightIntensity, Day));
		if (!Sky->bRealTimeCapture)
		{
			bSkyNeedsRecapture = true; // recaptured from Tick (throttled) while playing
		}
	}
	// City lights follow the night side: tell the globes the sun moved (needed in the editor, where they do not tick).
	for (TActorIterator<AGlobeVFXActor> It(GetWorld()); It; ++It)
	{
		It->UpdateCityLightsSun();
	}
	SetActorTickEnabled(NeedsTick());
}
