// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GlobeVoiceInputComponent.generated.h"

class FGlobeMicCapture;

/** Where the voice visualizer gets its signal. */
UENUM(BlueprintType)
enum class EGlobeVoiceSource : uint8
{
	Microphone,
	/** Repeating speech-like pattern (testing without talking). */
	SimulatedSpeech,
	/** Silent (the visualizer only idles). */
	Off,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGlobeListeningChanged, bool, bIsListening);

/**
 * Listens to the default microphone and turns it into what the voice visualizer draws: a smoothed level, a small
 * spectrum (NumBands bands, low to high, 80 Hz .. 5 kHz, the voice range) and a "listening" state (level above
 * ThresholdDb, held for HoldTime after the voice stops). The capture runs on the audio thread; Tick smooths on the
 * game thread. No speech recognition: this only detects that someone is talking.
 */
UCLASS(ClassGroup = (AIGlobe), meta = (BlueprintSpawnableComponent))
class VFX_API UGlobeVoiceInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGlobeVoiceInputComponent();

	/** Smoothed band levels 0..1 (low to high frequency). */
	const TArray<float>& GetBands() const { return Bands; }

	/** Smoothed overall level 0..1. */
	UFUNCTION(BlueprintPure, Category = "Globe Voice")
	float GetLevel() const { return Level; }

	UFUNCTION(BlueprintPure, Category = "Globe Voice")
	bool IsListening() const { return bListening; }

	/** Microphone level right now in dBFS (for setting Threshold Db). */
	UFUNCTION(BlueprintPure, Category = "Globe Voice")
	float GetLevelDb() const { return LevelDb; }

	UFUNCTION(BlueprintCallable, Category = "Globe Voice")
	void SetSource(EGlobeVoiceSource NewSource);

	UFUNCTION(BlueprintPure, Category = "Globe Voice")
	bool IsMicrophoneOpen() const;

	/** Fires when talking starts / stops (after the hold time). */
	UPROPERTY(BlueprintAssignable, Category = "Globe Voice")
	FGlobeListeningChanged OnListeningChanged;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Voice")
	EGlobeVoiceSource Source = EGlobeVoiceSource::Microphone;

	/** Number of spectrum bands (the visualizer uses one per bar on each side). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Voice", meta = (ClampMin = "2", ClampMax = "48"))
	int32 NumBands = 14;

	/** Voice is detected above this level (dBFS; quiet room ~ -60, normal speech ~ -30). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Voice", meta = (UIMin = "-80.0", UIMax = "-10.0"))
	float ThresholdDb = -42.0f;

	/** Gain on the bars (1 = normal speech fills them; raise for quiet microphones). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Voice", meta = (ClampMin = "0.1", UIMax = "5.0"))
	float Sensitivity = 1.0f;

	/** Seconds "listening" stays on after the voice drops below the threshold (bridges pauses between words). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Voice", meta = (ClampMin = "0.0", UIMax = "3.0", Units = "s"))
	float HoldTime = 0.8f;

	/** How fast the bars rise and fall (per second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Voice", meta = (ClampMin = "1.0", UIMax = "60.0"))
	float AttackSpeed = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Voice", meta = (ClampMin = "0.5", UIMax = "30.0"))
	float ReleaseSpeed = 6.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void OpenMicrophone();
	void CloseMicrophone();

	TSharedPtr<FGlobeMicCapture> Mic;
	TArray<float> Bands;
	TArray<float> RawBands;
	float Level = 0.0f;
	float LevelDb = -100.0f;
	bool bListening = false;
	float SinceVoice = 1000.0f;
	float SimTime = 0.0f;
};
