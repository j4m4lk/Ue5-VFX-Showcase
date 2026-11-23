// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GlobeTimeOfDay.generated.h"

class ADirectionalLight;
class ASkyLight;

/**
 * Time-of-day control for the level: moves the sun (a Directional Light flagged as the atmosphere sun) along a real
 * solar path for the chosen hour, latitude and season, fades it out below the horizon and dims the Sky Light toward
 * a night level. Sky Atmosphere, Volumetric Cloud and fog react to the sun on their own.
 * Works live in the editor (drag Time Of Day in the Details panel) and while playing; optional auto-advance.
 * Place one in the level (Place Actors > Globe Time Of Day). If the level has none, the viewer pawn spawns one at
 * play start so the [ / ] / T keys still work.
 */
UCLASS(ClassGroup = (AIGlobe), meta = (DisplayName = "Globe Time Of Day"))
class VFX_API AGlobeTimeOfDay : public AActor
{
	GENERATED_BODY()

public:
	AGlobeTimeOfDay();

	/** Hours, 0..24 (wraps). */
	UFUNCTION(BlueprintCallable, Category = "Time Of Day")
	void SetTimeOfDay(float Hours);

	UFUNCTION(BlueprintPure, Category = "Time Of Day")
	float GetTimeOfDay() const { return TimeOfDay; }

	/** Moves the time by Hours (negative = back), wrapping around midnight. */
	UFUNCTION(BlueprintCallable, Category = "Time Of Day")
	void AddHours(float Hours) { SetTimeOfDay(TimeOfDay + Hours); }

	UFUNCTION(BlueprintCallable, Category = "Time Of Day")
	void SetAutoAdvance(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Time Of Day")
	bool IsAutoAdvancing() const { return bAutoAdvance; }

	/** Hours per second for the viewer's [ / ] keys. */
	float GetScrubSpeed() const { return ScrubSpeed; }

	UFUNCTION(CallInEditor, Category = "Time Of Day|Presets")
	void Sunrise() { SetTimeOfDay(6.5f); }

	UFUNCTION(CallInEditor, Category = "Time Of Day|Presets")
	void Noon() { SetTimeOfDay(12.0f); }

	UFUNCTION(CallInEditor, Category = "Time Of Day|Presets")
	void Sunset() { SetTimeOfDay(18.5f); }

	UFUNCTION(CallInEditor, Category = "Time Of Day|Presets")
	void Night() { SetTimeOfDay(23.0f); }

	/** Re-applies the sun and sky for the current settings. */
	UFUNCTION(BlueprintCallable, Category = "Time Of Day")
	void ApplyTimeOfDay();

	/** The level's time-of-day actor, or null. */
	static AGlobeTimeOfDay* Find(const UWorld* World);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Hour of the day (0 = midnight, 12 = noon). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day", meta = (ClampMin = "0.0", ClampMax = "24.0", UIMin = "0.0", UIMax = "24.0"))
	float TimeOfDay = 15.0f;

	/** Advance the clock by itself while playing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day")
	bool bAutoAdvance = false;

	/** Real minutes for a full 24-hour day while auto-advancing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day", meta = (ClampMin = "0.1", UIMax = "60.0", Units = "min"))
	float DayLengthMinutes = 4.0f;

	/** Hours per second while holding [ or ] in play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day", meta = (ClampMin = "0.1", UIMax = "12.0"))
	float ScrubSpeed = 3.0f;

	/** Sun path: latitude (0 = equator, the sun passes overhead; higher = lower sun), season (sun declination,
	 *  +23.4 = northern summer, -23.4 = northern winter) and which way north is (yaw, degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Sun Path", meta = (ClampMin = "-89.0", ClampMax = "89.0", Units = "deg"))
	float Latitude = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Sun Path", meta = (ClampMin = "-23.44", ClampMax = "23.44", Units = "deg"))
	float SeasonDeclination = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Sun Path", meta = (UIMin = "-180.0", UIMax = "180.0", Units = "deg"))
	float NorthYaw = 0.0f;

	/** The sun. Empty = the level's first Directional Light. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Time Of Day|Lights")
	TObjectPtr<ADirectionalLight> Sun;

	/** Empty = the level's first Sky Light. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Time Of Day|Lights")
	TObjectPtr<ASkyLight> SkyLight;

	/** Sun intensity at day (lux). <= 0: taken from the sun's current intensity the first time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Lights", meta = (ClampMin = "0.0", Units = "lux"))
	float DaySunIntensity = 0.0f;

	/** Sky light intensity at day and at night. Day <= 0: taken from the sky light's current intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Lights", meta = (ClampMin = "0.0"))
	float DaySkyLightIntensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Lights", meta = (ClampMin = "0.0"))
	float NightSkyLightIntensity = 0.12f;

	/** Warm the sun toward orange near the horizon (on top of what the Sky Atmosphere already does). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Lights")
	bool bWarmLowSun = true;

	/** Make the sun and sky light Movable, the sun the atmosphere sun and the sky light capture in real time, so they
	 *  can change while playing. Off: leave their settings alone (rotation then needs Movable lights anyway). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Lights")
	bool bManageLightSettings = true;

	/** Hide static sky-sphere meshes (e.g. SM_SkySphere with M_SimpleSkyDome). Their painted sky cannot follow the
	 *  sun, and it covers the Sky Atmosphere that does. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time Of Day|Lights")
	bool bHideStaticSkyDome = true;

	/** Sun height above the horizon now (degrees), for reference. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Time Of Day", meta = (Units = "deg"))
	float SunElevation = 0.0f;

private:
	void ResolveLights();
	void ApplySkyDomeVisibility();
	bool NeedsTick() const;

	float SkyRecaptureTimer = 0.0f;
	bool bSkyNeedsRecapture = false;
};
