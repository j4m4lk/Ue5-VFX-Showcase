// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeCardEdgeFlow.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "GlobeCardEdgeFlow"

/** Leaf widget that forwards ticking and painting to its UGlobeCardEdgeFlow; it fills its slot. */
class SGlobeCardEdgeFlow : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGlobeCardEdgeFlow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGlobeCardEdgeFlow* InOwner)
	{
		Owner = InOwner;
		ForceVolatile(true); // animated every frame
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		if (UGlobeCardEdgeFlow* Flow = Owner.Get())
		{
			Flow->TickFlow(InDeltaTime);
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const UGlobeCardEdgeFlow* Flow = Owner.Get();
		return Flow ? Flow->PaintFlow(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle) : LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector; // takes the size of its slot (the card)
	}

private:
	TWeakObjectPtr<UGlobeCardEdgeFlow> Owner;
};

namespace
{
	/** Point at distance D along the perimeter of a rounded rectangle, clockwise from the top-left straight. */
	struct FRoundedPath
	{
		FVector2f Min, Max;
		float R = 0.0f;
		float Straight[4] = {};
		float Arc = 0.0f;
		float Length = 0.0f;

		FRoundedPath(const FVector2f& InMin, const FVector2f& InMax, float Radius)
			: Min(InMin), Max(InMax)
		{
			const FVector2f Size = Max - Min;
			R = FMath::Clamp(Radius, 0.0f, 0.5f * FMath::Min(Size.X, Size.Y));
			Straight[0] = Straight[2] = FMath::Max(0.0f, Size.X - 2.0f * R);
			Straight[1] = Straight[3] = FMath::Max(0.0f, Size.Y - 2.0f * R);
			Arc = 0.5f * UE_PI * R;
			Length = Straight[0] + Straight[1] + Straight[2] + Straight[3] + 4.0f * Arc;
		}

		FVector2f At(float D) const
		{
			if (Length <= 0.0f)
			{
				return Min;
			}
			D = D - FMath::FloorToFloat(D / Length) * Length;
			// Straight start, end and the following corner's centre / start angle, for the four sides.
			const FVector2f Starts[4] = { FVector2f(Min.X + R, Min.Y), FVector2f(Max.X, Min.Y + R), FVector2f(Max.X - R, Max.Y), FVector2f(Min.X, Max.Y - R) };
			const FVector2f Dirs[4] = { FVector2f(1, 0), FVector2f(0, 1), FVector2f(-1, 0), FVector2f(0, -1) };
			const FVector2f Centers[4] = { FVector2f(Max.X - R, Min.Y + R), FVector2f(Max.X - R, Max.Y - R), FVector2f(Min.X + R, Max.Y - R), FVector2f(Min.X + R, Min.Y + R) };
			const float StartAngles[4] = { -90.0f, 0.0f, 90.0f, 180.0f };
			for (int32 Side = 0; Side < 4; ++Side)
			{
				if (D <= Straight[Side])
				{
					return Starts[Side] + Dirs[Side] * D;
				}
				D -= Straight[Side];
				if (D <= Arc)
				{
					const float A = FMath::DegreesToRadians(StartAngles[Side] + 90.0f * (Arc > 0.0f ? D / Arc : 0.0f));
					return Centers[Side] + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R;
				}
				D -= Arc;
			}
			return Starts[0];
		}
	};
}

UGlobeCardEdgeFlow::UGlobeCardEdgeFlow()
{
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UGlobeCardEdgeFlow::RebuildWidget()
{
	MyFlow = SNew(SGlobeCardEdgeFlow, this);
	return MyFlow.ToSharedRef();
}

void UGlobeCardEdgeFlow::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyFlow.Reset();
}

#if WITH_EDITOR
const FText UGlobeCardEdgeFlow::GetPaletteCategory()
{
	return LOCTEXT("Palette", "AIGlobe");
}
#endif

void UGlobeCardEdgeFlow::TickFlow(float DeltaTime)
{
	Time = FMath::Fmod(Time + FMath::Min(DeltaTime, 0.1f), 3600.0f);
}

int32 UGlobeCardEdgeFlow::PaintFlow(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	if (Colors.Num() == 0 || ParticleCount <= 0)
	{
		return LayerId;
	}
	const FVector2f Size = FVector2f(Geometry.GetLocalSize());
	const FRoundedPath Path(FVector2f(Inset, Inset), Size - FVector2f(Inset, Inset), CornerRadius - Inset);
	if (Path.Length <= 1.0f)
	{
		return LayerId;
	}
	const float Tint = Opacity * InWidgetStyle.GetColorAndOpacityTint().A;
	const float Direction = Speed < 0.0f ? -1.0f : 1.0f;
	// Wraps every 3600 s; Speed * 3600 whole laps keep it seamless for typical speeds.
	const float Travel = Time * Speed;

	auto Dot = [&](int32 Layer, const FVector2f& Center, float Diameter, FLinearColor Color, float Alpha)
	{
		Color.A = FMath::Clamp(Alpha * Tint, 0.0f, 1.0f);
		if (Color.A <= 0.003f)
		{
			return;
		}
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, Diameter * 0.5f);
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Diameter, Diameter), FSlateLayoutTransform(Center - FVector2f(Diameter, Diameter) * 0.5f)),
			&Brush, ESlateDrawEffect::None, Color);
	};

	constexpr int32 GroupSize = 3;
	for (int32 i = 0; i < ParticleCount; ++i)
	{
		// Even spacing, pulled toward small groups by Clumping.
		const float Even = (float)i / ParticleCount;
		const float Grouped = (float)(i / GroupSize * GroupSize) / ParticleCount + (i % GroupSize) * 0.015f;
		const float Head = (FMath::Lerp(Even, Grouped, Clumping) + Travel) * Path.Length;
		const FLinearColor Color = Colors[i % Colors.Num()];
		const float Twinkle = 0.8f + 0.2f * FMath::Sin(Time * 5.0f + i * 1.7f);

		// Trail first (behind), then the glow and the head on top.
		for (int32 k = TrailLength; k >= 1; --k)
		{
			const float Falloff = 1.0f - (float)k / (TrailLength + 1);
			Dot(LayerId, Path.At(Head - Direction * k * TrailSpacing), ParticleSize * FMath::Lerp(0.45f, 0.95f, Falloff), Color, 0.7f * Falloff * Falloff * Twinkle);
		}
		const FVector2f P = Path.At(Head);
		if (Glow > 0.0f)
		{
			Dot(LayerId, P, ParticleSize * 3.2f, Color, 0.3f * Glow * Twinkle);
		}
		Dot(LayerId + 1, P, ParticleSize, FMath::Lerp(Color, FLinearColor::White, 0.35f), Twinkle);
	}
	return LayerId + 1;
}

#undef LOCTEXT_NAMESPACE
