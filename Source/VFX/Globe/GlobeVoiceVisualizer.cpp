// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeVoiceVisualizer.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Globe/GlobeVoiceInputComponent.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "GlobeVoiceVisualizer"

/** Leaf widget that forwards ticking, painting and sizing to its UGlobeVoiceVisualizer. */
class SGlobeVoiceVisualizer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGlobeVoiceVisualizer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGlobeVoiceVisualizer* InOwner)
	{
		Owner = InOwner;
		ForceVolatile(true); // animated every frame
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		if (UGlobeVoiceVisualizer* Visualizer = Owner.Get())
		{
			Visualizer->TickVisual(InDeltaTime);
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const UGlobeVoiceVisualizer* Visualizer = Owner.Get();
		return Visualizer ? Visualizer->PaintVisual(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle) : LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const UGlobeVoiceVisualizer* Visualizer = Owner.Get();
		return Visualizer ? Visualizer->ComputeSize() : FVector2D(100.0f, 100.0f);
	}

private:
	TWeakObjectPtr<UGlobeVoiceVisualizer> Owner;
};

namespace
{
	/** Filled circle / bar via a rounded-box brush (radius = half the short side when rounded). */
	void DrawBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, const FVector2f& Size, const FLinearColor& Color, bool bRounded = true)
	{
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, bRounded ? FMath::Min(Size.X, Size.Y) * 0.5f : 0.0f);
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Center - Size * 0.5f)), &Brush, ESlateDrawEffect::None, Color);
	}

	/** Ellipse outline (Rx may be ~0 for a meridian seen edge-on). */
	void DrawEllipse(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, float Rx, float Ry, const FLinearColor& Color, float Thickness)
	{
		TArray<FVector2f> Points;
		constexpr int32 Segments = 48;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float A = UE_TWO_PI * i / Segments;
			Points.Add(Center + FVector2f(FMath::Cos(A) * Rx, FMath::Sin(A) * Ry));
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	}
}

UGlobeVoiceVisualizer::UGlobeVoiceVisualizer()
{
	IdleText = LOCTEXT("Idle", "Awaiting Command");
	ListeningText = LOCTEXT("Listening", "Listening");
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible); // never blocks clicks on the cards or the globe
}

void UGlobeVoiceVisualizer::SetVoiceInput(UGlobeVoiceInputComponent* InVoice)
{
	Voice = InVoice;
}

FVector2D UGlobeVoiceVisualizer::ComputeSize() const
{
	const float Half = GlobeSize * 0.5f + SideSpan();
	const float Height = FMath::Max(PeakHeight(), GlobeSize * 1.6f) + TextGap + TextSize * 2.0f + 8.0f;
	return FVector2D(Half * 2.0f + 20.0f, Height);
}

float UGlobeVoiceVisualizer::SideSpan() const
{
	return Style == EGlobeVoiceVisualizerStyle::Waves ? WaveGap() + WaveSpan : BarGap * 2.0f + BarsPerSide * (BarWidth + BarGap);
}

float UGlobeVoiceVisualizer::PeakHeight() const
{
	return Style == EGlobeVoiceVisualizerStyle::Waves ? WaveHeight + WaveThickness * 4.0f : MaxBarHeight;
}

float UGlobeVoiceVisualizer::SampleBands(float Out01, float Level) const
{
	const TArray<float>* Bands = GetBandsNow();
	const float F = FMath::Clamp(Out01, 0.0f, 1.0f) * FMath::Max(0, BarsPerSide - 1);
	float Value = 0.0f;
	if (Bands && Bands->Num() > 0)
	{
		const int32 I0 = FMath::Min(FMath::FloorToInt32(F), Bands->Num() - 1);
		const int32 I1 = FMath::Min(I0 + 1, Bands->Num() - 1);
		Value = FMath::Lerp((*Bands)[I0], (*Bands)[I1], F - FMath::FloorToFloat(F)) * BarGain;
	}
	// Idle: a slow breathing ripple running outward, gone once the voice takes over.
	return FMath::Max(Value, IdleRipple * (0.75f + 0.25f * FMath::Sin(Time * 2.2f - F * 0.55f)) * (1.0f - Level));
}

TSharedRef<SWidget> UGlobeVoiceVisualizer::RebuildWidget()
{
	MyVisualizer = SNew(SGlobeVoiceVisualizer, this);
	return MyVisualizer.ToSharedRef();
}

void UGlobeVoiceVisualizer::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyVisualizer.IsValid())
	{
		MyVisualizer->Invalidate(EInvalidateWidgetReason::Layout); // size follows the settings
	}
}

void UGlobeVoiceVisualizer::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyVisualizer.Reset();
}

#if WITH_EDITOR
const FText UGlobeVoiceVisualizer::GetPaletteCategory()
{
	return LOCTEXT("Palette", "AIGlobe");
}
#endif

bool UGlobeVoiceVisualizer::IsListeningNow() const
{
	return Voice.IsValid() ? Voice->IsListening() : bPreviewListening;
}

float UGlobeVoiceVisualizer::GetLevelNow() const
{
	return Voice.IsValid() ? Voice->GetLevel() : PreviewLevel;
}

const TArray<float>* UGlobeVoiceVisualizer::GetBandsNow() const
{
	return Voice.IsValid() ? &Voice->GetBands() : &PreviewBands;
}

void UGlobeVoiceVisualizer::TickVisual(float DeltaTime)
{
	DeltaTime = FMath::Min(DeltaTime, 0.1f);
	Time += DeltaTime;
	if (UGlobeVoiceInputComponent* Input = Voice.Get(); Input && Input->NumBands != BarsPerSide)
	{
		Input->NumBands = BarsPerSide; // one analysed band per bar
	}

	// Preview / no microphone: a speech-like pattern (syllables of varying loudness, energy around the low-mid bands).
	PreviewBands.SetNumZeroed(BarsPerSide);
	const float Syllable = FMath::Max(0.0f, FMath::Sin(Time * 7.0f)) * (0.55f + 0.45f * FMath::Sin(Time * 1.3f + 1.0f));
	PreviewLevel = bPreviewListening ? Syllable : 0.0f;
	for (int32 i = 0; i < BarsPerSide; ++i)
	{
		const float X = (float)i / FMath::Max(1, BarsPerSide - 1);
		const float Shape = FMath::Exp(-FMath::Square((X - 0.25f) / 0.35f)) * (0.75f + 0.25f * FMath::Sin(Time * 11.0f + i * 1.7f));
		PreviewBands[i] = PreviewLevel * Shape;
	}

	WavePhase = FMath::Fmod(WavePhase + DeltaTime * WaveSpeed * (1.0f + 1.5f * GetLevelNow()) * UE_TWO_PI, UE_TWO_PI * 1000.0f);
	ListeningBlend += ((IsListeningNow() ? 1.0f : 0.0f) - ListeningBlend) * (1.0f - FMath::Exp(-8.0f * DeltaTime));
}

int32 UGlobeVoiceVisualizer::PaintVisual(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const FVector2f Size = FVector2f(Geometry.GetLocalSize());
	const float Alpha = FMath::Clamp(Opacity, 0.0f, 1.0f) * InWidgetStyle.GetColorAndOpacityTint().A;
	const float Level = GetLevelNow();
	const float Radius = GlobeSize * 0.5f;
	const float SideCenterY = FMath::Max(PeakHeight(), GlobeSize * 1.6f) * 0.5f + 4.0f;
	const FVector2f Center(Size.X * 0.5f, SideCenterY);
	auto WithAlpha = [Alpha](FLinearColor C, float A) { C.A = FMath::Clamp(A * Alpha, 0.0f, 1.0f); return C; };

	// --- Either side of the globe: spectrum bars or waves (layers LayerId, LayerId + 1).
	if (Style == EGlobeVoiceVisualizerStyle::Waves)
	{
		PaintWaves(Geometry, OutDrawElements, LayerId, Center, Alpha, Level);
	}
	else
	{
		PaintBars(Geometry, OutDrawElements, LayerId, Center, Alpha, Level);
	}

	// --- Small wireframe globe: soft glow (pulses with the voice), translucent body, rim, turning meridians, latitudes.
	const int32 GlobeLayer = LayerId + 2;
	const float Glow = Radius * (1.45f + 0.35f * Level);
	DrawBox(OutDrawElements, GlobeLayer, Geometry, Center, FVector2f(Glow * 2.0f), WithAlpha(Color, (0.10f + 0.20f * Level) * GlobeGlow));
	DrawBox(OutDrawElements, GlobeLayer, Geometry, Center, FVector2f(Radius * 2.0f), WithAlpha(Color * 0.35f, 0.55f));
	const FLinearColor LineColor = WithAlpha(Color, 0.55f + 0.35f * Level);
	DrawEllipse(OutDrawElements, GlobeLayer + 1, Geometry, Center, Radius, Radius, WithAlpha(Color, 0.95f), GlobeLineThickness * 1.4f);
	const float Spin = Time * (GlobeSpin + GlobeSpinWithVoice * Level);
	for (int32 k = 0; k < 3; ++k)
	{
		const float Phase = Spin + k * UE_PI / 3.0f;
		DrawEllipse(OutDrawElements, GlobeLayer + 1, Geometry, Center, Radius * FMath::Abs(FMath::Cos(Phase)), Radius, LineColor, GlobeLineThickness);
	}
	for (const float Lat : { -0.55f, 0.0f, 0.55f })
	{
		const float Y = Radius * FMath::Sin(Lat);
		const float W = Radius * FMath::Cos(Lat);
		DrawEllipse(OutDrawElements, GlobeLayer + 1, Geometry, Center + FVector2f(0.0f, Y), W, W * 0.18f, LineColor, GlobeLineThickness);
	}

	// --- Status text under the globe: idle and listening texts cross-fade; listening gently pulses.
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", TextSize);
	Font.LetterSpacing = TextLetterSpacing;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float TextY = Center.Y + FMath::Max(Radius * 1.45f, PeakHeight() * 0.5f) + TextGap;
	auto DrawCentered = [&](const FText& Text, float TextAlpha)
	{
		if (TextAlpha <= 0.01f || Text.IsEmpty())
		{
			return;
		}
		const FString String = Text.ToString();
		const FVector2f TextSize2D = FVector2f(Measure->Measure(String, Font));
		const FVector2f Offset(Center.X - TextSize2D.X * 0.5f, TextY);
		FSlateDrawElement::MakeText(OutDrawElements, GlobeLayer + 2, Geometry.ToPaintGeometry(TextSize2D, FSlateLayoutTransform(Offset)), String, Font,
			ESlateDrawEffect::None, WithAlpha(TextColor, TextAlpha));
	};
	DrawCentered(IdleText, 1.0f - ListeningBlend);
	DrawCentered(ListeningText, ListeningBlend * (0.8f + 0.2f * FMath::Sin(Time * 4.0f)));

	return GlobeLayer + 2;
}

void UGlobeVoiceVisualizer::PaintBars(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FVector2f& Center, float Alpha, float Level) const
{
	// Mirrored spectrum bars, the outer ones fading away.
	const float Radius = GlobeSize * 0.5f;
	for (int32 i = 0; i < BarsPerSide; ++i)
	{
		const float Out01 = BarsPerSide > 1 ? (float)i / (BarsPerSide - 1) : 0.0f;
		const float Height = FMath::Lerp(MinBarHeight, MaxBarHeight, FMath::Clamp(SampleBands(Out01, Level), 0.0f, 1.0f));
		FLinearColor BarColor = Color;
		BarColor.A = FMath::Clamp(Alpha * FMath::Pow(1.0f - Out01, EdgeFade), 0.0f, 1.0f); // the outermost bar fades to nothing
		for (const float Side : { -1.0f, 1.0f })
		{
			const float X = Center.X + Side * (Radius + BarGap * 2.0f + i * (BarWidth + BarGap) + BarWidth * 0.5f);
			DrawBox(Out, LayerId, Geometry, FVector2f(X, Center.Y), FVector2f(BarWidth, Height), BarColor, bRoundedBars);
		}
	}
}

void UGlobeVoiceVisualizer::PaintWaves(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FVector2f& Center, float Alpha, float Level) const
{
	// Layered wave lines travelling outward from the globe. Their height follows the voice spectrum along the
	// span (low bands near the globe), they swell in from the globe edge and fade toward the outer ends.
	static constexpr float LineFrequency[] = { 1.0f, 1.37f, 0.71f, 1.83f, 0.56f, 1.18f };
	static constexpr float LinePhase[] = { 0.0f, 2.1f, 4.0f, 1.2f, 3.1f, 5.3f };
	const float Start = GlobeSize * 0.5f + WaveGap();
	const int32 Count = FMath::Clamp(FMath::CeilToInt32(WaveSpan / 3.0f), 8, 400) + 1;
	const int32 Lines = FMath::Clamp(WaveLines, 1, 6);

	// Precompute the height envelope and fade along the span (shared by all lines and both sides).
	TArray<float> Envelope, Fade;
	Envelope.SetNumUninitialized(Count);
	Fade.SetNumUninitialized(Count);
	for (int32 p = 0; p < Count; ++p)
	{
		const float X01 = (float)p / (Count - 1);
		const float SwellIn = FMath::SmoothStep(0.0f, 0.18f, X01); // pinned to the centre line at the globe
		// Half spectrum shape, half overall loudness, so the whole wave swells (voice energy sits in the low bands).
		const float Value = 0.5f * SampleBands(X01, Level) + 0.5f * Level * BarGain;
		Envelope[p] = 0.5f * WaveHeight * FMath::Clamp(Value, 0.0f, 1.0f) * SwellIn;
		Fade[p] = FMath::Pow(1.0f - X01, EdgeFade) * FMath::SmoothStep(0.0f, 0.06f, X01);
	}

	TArray<FVector2f> Points;
	TArray<FLinearColor> Colors;
	Points.SetNumUninitialized(Count);
	Colors.SetNumUninitialized(Count);
	auto Emit = [&](int32 Layer, float LineAlpha, float Thickness)
	{
		for (int32 p = 0; p < Count; ++p)
		{
			Colors[p] = Color;
			Colors[p].A = FMath::Clamp(Alpha * LineAlpha * Fade[p], 0.0f, 1.0f);
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points, Colors, ESlateDrawEffect::None, FLinearColor::White, true, Thickness);
	};

	for (const float Side : { -1.0f, 1.0f })
	{
		if (bWaveBaseline)
		{
			for (int32 p = 0; p < Count; ++p)
			{
				Points[p] = FVector2f(Center.X + Side * (Start + WaveSpan * p / (Count - 1)), Center.Y);
			}
			Emit(LayerId, 0.22f, 1.0f);
		}
		for (int32 k = 0; k < Lines; ++k)
		{
			const float K = UE_TWO_PI / (WaveLength * LineFrequency[k]);
			// Each line breathes a little on its own so the layers cross and separate as they move up and down.
			const float Amp = (1.0f / (1.0f + 0.45f * k)) * (0.78f + 0.22f * FMath::Sin(Time * (2.3f + 0.7f * k) + LinePhase[k]));
			for (int32 p = 0; p < Count; ++p)
			{
				const float D = WaveSpan * p / (Count - 1);
				const float Y = Envelope[p] * Amp * FMath::Sin(K * D - WavePhase + LinePhase[k]);
				Points[p] = FVector2f(Center.X + Side * (Start + D), Center.Y + Y);
			}
			const float LineAlpha = 1.0f / (1.0f + 0.6f * k);
			if (WaveGlow > 0.0f)
			{
				Emit(LayerId, 0.16f * WaveGlow * LineAlpha, WaveThickness * 4.0f); // soft glow under the line
			}
			Emit(LayerId + 1, LineAlpha, WaveThickness);
		}
	}
}

#undef LOCTEXT_NAMESPACE
