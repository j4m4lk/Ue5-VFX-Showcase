// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeStatIcon.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "GlobeStatIcon"

/** Leaf widget that forwards painting and sizing to its UGlobeStatIcon. */
class SGlobeStatIcon : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGlobeStatIcon) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGlobeStatIcon* InOwner)
	{
		Owner = InOwner;
	}

	void SetTexture(UTexture2D* Texture)
	{
		TextureBrush = FSlateBrush();
		if (Texture)
		{
			TextureBrush.SetResourceObject(Texture);
			TextureBrush.DrawAs = ESlateBrushDrawType::Image;
		}
		bHasTexture = Texture != nullptr;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const UGlobeStatIcon* Icon = Owner.Get();
		if (!Icon)
		{
			return LayerId;
		}
		if (bHasTexture)
		{
			const FVector2f Local = FVector2f(AllottedGeometry.GetLocalSize());
			const float S = Icon->Size;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(S, S), FSlateLayoutTransform((Local - FVector2f(S, S)) * 0.5f)),
				&TextureBrush, ESlateDrawEffect::None, Icon->Color * InWidgetStyle.GetColorAndOpacityTint());
		}
		return Icon->PaintIcon(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const UGlobeStatIcon* Icon = Owner.Get();
		return Icon ? Icon->ComputeSize() : FVector2D(18.0f, 18.0f);
	}

private:
	TWeakObjectPtr<UGlobeStatIcon> Owner;
	FSlateBrush TextureBrush;
	bool bHasTexture = false;
};

UGlobeStatIcon::UGlobeStatIcon()
{
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

FVector2D UGlobeStatIcon::ComputeSize() const
{
	const float D = Size * FMath::Max(1.0f, DiscOpacity > 0.0f ? DiscScale : 1.0f);
	return FVector2D(D, D);
}

TSharedRef<SWidget> UGlobeStatIcon::RebuildWidget()
{
	MyIcon = SNew(SGlobeStatIcon, this);
	MyIcon->SetTexture(Texture);
	return MyIcon.ToSharedRef();
}

void UGlobeStatIcon::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyIcon.IsValid())
	{
		MyIcon->SetTexture(Texture);
		MyIcon->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void UGlobeStatIcon::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyIcon.Reset();
}

#if WITH_EDITOR
const FText UGlobeStatIcon::GetPaletteCategory()
{
	return LOCTEXT("Palette", "AIGlobe");
}
#endif

int32 UGlobeStatIcon::PaintIcon(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	const FVector2f Local = FVector2f(Geometry.GetLocalSize());
	const FVector2f Center = Local * 0.5f;
	const float S = Size;
	const FVector2f Origin = Center - FVector2f(S, S) * 0.5f;
	FLinearColor Ink = Color * InWidgetStyle.GetColorAndOpacityTint();
	const float W = LineThickness;

	// Soft disc behind the icon.
	if (DiscOpacity > 0.0f)
	{
		const float D = S * DiscScale;
		const FSlateRoundedBoxBrush Disc(FLinearColor::White, D * 0.5f);
		FLinearColor DiscColor = Ink;
		DiscColor.A *= DiscOpacity;
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(FVector2f(D, D), FSlateLayoutTransform(Center - FVector2f(D, D) * 0.5f)), &Disc, ESlateDrawEffect::None, DiscColor);
	}
	if (Texture || Icon == EGlobeStatIcon::None)
	{
		return LayerId + 1;
	}

	const int32 Layer = LayerId + 1;
	// Icons are drawn in a unit square (0..1, y down) mapped onto the Size x Size box.
	auto P = [&Origin, S](float X, float Y) { return Origin + FVector2f(X, Y) * S; };
	auto Line = [&](std::initializer_list<FVector2f> Points, float Thickness = -1.0f, float Alpha = 1.0f)
	{
		FLinearColor C = Ink;
		C.A *= Alpha;
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), TArray<FVector2f>(Points), ESlateDrawEffect::None, C, true, Thickness > 0.0f ? Thickness : W);
	};
	auto Arc = [&](float Cx, float Cy, float Rx, float Ry, float A0Deg, float A1Deg, float Alpha = 1.0f)
	{
		TArray<FVector2f> Points;
		const int32 Steps = FMath::Max(6, FMath::CeilToInt32(FMath::Abs(A1Deg - A0Deg) / 10.0f));
		for (int32 i = 0; i <= Steps; ++i)
		{
			const float A = FMath::DegreesToRadians(FMath::Lerp(A0Deg, A1Deg, (float)i / Steps));
			Points.Add(P(Cx + FMath::Cos(A) * Rx, Cy + FMath::Sin(A) * Ry));
		}
		FLinearColor C = Ink;
		C.A *= Alpha;
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, C, true, W);
	};
	auto Circle = [&](float Cx, float Cy, float R, float Alpha = 1.0f) { Arc(Cx, Cy, R, R, 0.0f, 360.0f, Alpha); };
	auto Rect = [&](float X0, float Y0, float X1, float Y1) { Line({ P(X0, Y0), P(X1, Y0), P(X1, Y1), P(X0, Y1), P(X0, Y0) }); };
	auto Person = [&](float Cx, float Scale, float Alpha)
	{
		Circle(Cx, 0.3f, 0.15f * Scale, Alpha);
		Arc(Cx, 0.95f, 0.3f * Scale, 0.33f * Scale, 180.0f, 360.0f, Alpha); // shoulders
	};

	switch (Icon)
	{
	case EGlobeStatIcon::Person:
		Person(0.5f, 1.0f, 1.0f);
		break;
	case EGlobeStatIcon::People:
		Person(0.66f, 0.85f, 0.55f); // the one behind
		Person(0.4f, 1.0f, 1.0f);
		break;
	case EGlobeStatIcon::Building:
		Line({ P(0.1f, 0.4f), P(0.5f, 0.1f), P(0.9f, 0.4f) }); // roof
		Rect(0.2f, 0.4f, 0.8f, 0.9f);
		Line({ P(0.42f, 0.9f), P(0.42f, 0.68f), P(0.58f, 0.68f), P(0.58f, 0.9f) }); // door
		Line({ P(0.3f, 0.52f), P(0.38f, 0.52f) });
		Line({ P(0.62f, 0.52f), P(0.7f, 0.52f) });
		break;
	case EGlobeStatIcon::CheckBadge:
		Circle(0.5f, 0.5f, 0.42f);
		Line({ P(0.3f, 0.52f), P(0.44f, 0.66f), P(0.71f, 0.37f) }, W * 1.2f);
		break;
	case EGlobeStatIcon::Fingerprint:
		// Concentric open arcs, like a fingerprint's ridges.
		for (int32 k = 0; k < 4; ++k)
		{
			const float R = 0.1f + 0.11f * k;
			Arc(0.5f, 0.55f, R, R * 1.15f, 180.0f + 12.0f * k, 360.0f + 30.0f - 8.0f * k, 1.0f - 0.12f * k);
		}
		Line({ P(0.5f, 0.55f), P(0.5f, 0.78f) });
		break;
	case EGlobeStatIcon::Document:
		Line({ P(0.22f, 0.08f), P(0.62f, 0.08f), P(0.8f, 0.26f), P(0.8f, 0.92f), P(0.22f, 0.92f), P(0.22f, 0.08f) });
		Line({ P(0.62f, 0.08f), P(0.62f, 0.26f), P(0.8f, 0.26f) });
		for (const float Y : { 0.45f, 0.6f, 0.75f })
		{
			Line({ P(0.34f, Y), P(0.68f, Y) }, -1.0f, 0.8f);
		}
		break;
	case EGlobeStatIcon::Globe:
		Circle(0.5f, 0.5f, 0.42f);
		Arc(0.5f, 0.5f, 0.18f, 0.42f, 0.0f, 360.0f, 0.8f);
		Line({ P(0.08f, 0.5f), P(0.92f, 0.5f) }, -1.0f, 0.8f);
		break;
	case EGlobeStatIcon::Clock:
		Circle(0.5f, 0.5f, 0.42f);
		Line({ P(0.5f, 0.25f), P(0.5f, 0.5f), P(0.68f, 0.62f) });
		break;
	case EGlobeStatIcon::Chart:
		Line({ P(0.12f, 0.9f), P(0.88f, 0.9f) }, -1.0f, 0.7f);
		Rect(0.2f, 0.6f, 0.34f, 0.9f);
		Rect(0.43f, 0.38f, 0.57f, 0.9f);
		Rect(0.66f, 0.16f, 0.8f, 0.9f);
		break;
	default:
		break;
	}
	return Layer;
}

#undef LOCTEXT_NAMESPACE
