// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "GlobeVoiceVisualizer.generated.h"

class SGlobeVoiceVisualizer;
class UGlobeVoiceInputComponent;

/** What is drawn either side of the centre globe. */
UENUM(BlueprintType)
enum class EGlobeVoiceVisualizerStyle : uint8
{
	/** Mirrored spectrum bars. */
	Bars,
	/** Layered glowing wave lines that swell up and down with the voice. */
	Waves,
};

/**
 * Voice visualizer UMG widget: a small spinning wireframe globe with mirrored spectrum bars or waves (Style) on its
 * left and right (fading away toward the ends) and a status line under the globe that cross-fades from Idle Text ("Awaiting Command")
 * to Listening Text while the user talks. Drawn procedurally (no textures); transparent, with an overall Alpha.
 * Found in the Designer palette under AIGlobe; all its settings are in the Details panel and preview live in the
 * Designer (tick Preview Listening to see the talking state). Its size follows its settings (use Size To Content).
 */
UCLASS(meta = (DisplayName = "Globe Voice Visualizer"))
class VFX_API UGlobeVoiceVisualizer : public UWidget
{
	GENERATED_BODY()

public:
	UGlobeVoiceVisualizer();

	/** Where the bars and the listening state come from (the viewer pawn sets its microphone component). */
	UFUNCTION(BlueprintCallable, Category = "Voice Visualizer")
	void SetVoiceInput(UGlobeVoiceInputComponent* InVoice);

	/** Size the widget needs for the current settings (slate units). */
	FVector2D ComputeSize() const;

	// ---------------- Style ----------------

	/** Bars (spectrum bars) or Waves (wave lines that go up and down with the voice). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Style")
	EGlobeVoiceVisualizerStyle Style = EGlobeVoiceVisualizerStyle::Bars;

	// ---------------- Text ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Text")
	FText IdleText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Text")
	FText ListeningText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Text", meta = (ClampMin = "6", ClampMax = "64"))
	int32 TextSize = 18;

	/** Extra letter spacing (1/1000 em). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Text", meta = (UIMin = "0", UIMax = "600"))
	int32 TextLetterSpacing = 180;

	/** Gap between the globe / bars and the text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Text", meta = (UIMin = "0.0", UIMax = "60.0"))
	float TextGap = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Text", meta = (HideAlphaChannel))
	FLinearColor TextColor = FLinearColor(0.75f, 0.92f, 1.0f, 1.0f);

	// ---------------- Look ----------------

	/** Overall transparency of the whole visualizer (0 = invisible, 1 = solid). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Look", meta = (DisplayName = "Alpha", ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.85f;

	/** Bars and globe colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Look", meta = (HideAlphaChannel))
	FLinearColor Color = FLinearColor(0.15f, 0.75f, 1.0f, 1.0f);

	// ---------------- Response (both styles) ----------------

	/** Spectrum bands analysed: one bar per band on each side (Bars), or the points the waves follow (Waves). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Response", meta = (DisplayName = "Bars / Bands Per Side", ClampMin = "2", ClampMax = "48"))
	int32 BarsPerSide = 16;

	/** Multiplies the bar / wave heights (on top of the microphone Sensitivity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Response", meta = (DisplayName = "Gain", ClampMin = "0.1", UIMax = "4.0"))
	float BarGain = 1.0f;

	/** How tall the slow idle ripple gets while nobody talks (0..1 of the full height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Response", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IdleRipple = 0.08f;

	/** How quickly the bars / waves fade toward the outer ends (higher = fade starts sooner). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Response", meta = (ClampMin = "0.2", UIMax = "4.0"))
	float EdgeFade = 1.4f;

	// ---------------- Bars ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Bars", meta = (ClampMin = "1.0", UIMax = "30.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Bars", EditConditionHides))
	float BarWidth = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Bars", meta = (ClampMin = "0.0", UIMax = "30.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Bars", EditConditionHides))
	float BarGap = 6.0f;

	/** Height of a silent bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Bars", meta = (ClampMin = "1.0", UIMax = "60.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Bars", EditConditionHides))
	float MinBarHeight = 6.0f;

	/** Height of a bar at full voice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Bars", meta = (ClampMin = "4.0", UIMax = "300.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Bars", EditConditionHides))
	float MaxBarHeight = 120.0f;

	/** Round bar ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Bars", meta = (EditCondition = "Style == EGlobeVoiceVisualizerStyle::Bars", EditConditionHides))
	bool bRoundedBars = true;

	// ---------------- Waves ----------------

	/** Length of the waves on each side of the globe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (ClampMin = "20.0", UIMax = "600.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	float WaveSpan = 210.0f;

	/** Peak-to-peak height of the waves at full voice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (ClampMin = "4.0", UIMax = "300.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	float WaveHeight = 120.0f;

	/** Distance between two crests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (ClampMin = "8.0", UIMax = "300.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	float WaveLength = 56.0f;

	/** How fast the waves travel outward (crests per second); they speed up while talking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (ClampMin = "0.0", UIMax = "6.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	float WaveSpeed = 0.8f;

	/** Layered wave lines (each a little different in length, phase and brightness). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (ClampMin = "1", ClampMax = "6", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	int32 WaveLines = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (ClampMin = "0.5", UIMax = "8.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	float WaveThickness = 2.0f;

	/** Soft glow under each line (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (ClampMin = "0.0", UIMax = "2.0", EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	float WaveGlow = 1.0f;

	/** Faint horizontal centre line the waves ride on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Waves", meta = (EditCondition = "Style == EGlobeVoiceVisualizerStyle::Waves", EditConditionHides))
	bool bWaveBaseline = true;

	// ---------------- Globe ----------------

	/** Diameter of the centre globe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Globe", meta = (ClampMin = "8.0", UIMax = "300.0"))
	float GlobeSize = 72.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Globe", meta = (ClampMin = "0.25", UIMax = "6.0"))
	float GlobeLineThickness = 1.5f;

	/** Strength of the soft glow around the globe (it pulses with the voice). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Globe", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float GlobeGlow = 1.0f;

	/** Meridian spin while idle / extra spin at full voice (radians per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Globe", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float GlobeSpin = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Globe", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float GlobeSpinWithVoice = 1.4f;

	// ---------------- Preview ----------------

	/** Designer / no-microphone preview: shows the talking state with a simulated voice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Visualizer|Preview")
	bool bPreviewListening = false;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

	/** Called by the Slate widget. */
	void TickVisual(float DeltaTime);
	int32 PaintVisual(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SGlobeVoiceVisualizer> MyVisualizer;
	TWeakObjectPtr<UGlobeVoiceInputComponent> Voice;
	float Time = 0.0f;
	/** 0 = idle text, 1 = listening text (cross-fade). */
	float ListeningBlend = 0.0f;
	/** Outward travel of the waves (radians; speeds up with the voice, accumulated so it never jumps). */
	float WavePhase = 0.0f;
	/** Preview-only simulated bands and level. */
	TArray<float> PreviewBands;
	float PreviewLevel = 0.0f;

	bool IsListeningNow() const;
	float GetLevelNow() const;
	const TArray<float>* GetBandsNow() const;
	/** Band value 0..1 at Out01 (0 = next to the globe, 1 = outer end), interpolated, with gain and idle ripple. */
	float SampleBands(float Out01, float Level) const;
	/** Width taken on each side of the globe, and the tallest the bars / waves get. */
	float SideSpan() const;
	float PeakHeight() const;
	/** Space between the globe and the start of the waves. */
	float WaveGap() const { return 6.0f + GlobeSize * 0.08f; }
	void PaintBars(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FVector2f& Center, float Alpha, float Level) const;
	void PaintWaves(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FVector2f& Center, float Alpha, float Level) const;
};
