// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "GlobeCardEdgeFlow.generated.h"

class SGlobeCardEdgeFlow;

/**
 * Glowing particles with short trails that run around the edge of a rounded rectangle (the glass card's border),
 * coloured from Colors in turn (e.g. a country's flag colours). Fills its slot; drawn procedurally, every frame.
 * UGlobeStatCardWidget adds one over the card and feeds it the settings from its "Country Flow" section.
 */
UCLASS(meta = (DisplayName = "Globe Card Edge Flow"))
class VFX_API UGlobeCardEdgeFlow : public UWidget
{
	GENERATED_BODY()

public:
	UGlobeCardEdgeFlow();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow")
	TArray<FLinearColor> Colors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0", ClampMax = "200"))
	int32 ParticleCount = 24;

	/** Laps around the card per second (negative = the other way). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Speed = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0.5", UIMax = "16.0"))
	float ParticleSize = 4.0f;

	/** Dots in each particle's fading trail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0", ClampMax = "30"))
	int32 TrailLength = 7;

	/** Pixels between trail dots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0.5", UIMax = "20.0"))
	float TrailSpacing = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.9f;

	/** Soft halo around each particle (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float Glow = 0.6f;

	/** Corner radius of the path (match the card's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0.0", UIMax = "60.0"))
	float CornerRadius = 12.0f;

	/** Moves the path inside (+) or outside (-) the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (UIMin = "-20.0", UIMax = "20.0"))
	float Inset = 0.0f;

	/** Particles bunch into groups (0 = evenly spaced, 1 = tight groups). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge Flow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Clumping = 0.5f;

	void TickFlow(float DeltaTime);
	int32 PaintFlow(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	// UWidget
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SGlobeCardEdgeFlow> MyFlow;
	float Time = 0.0f;
};
