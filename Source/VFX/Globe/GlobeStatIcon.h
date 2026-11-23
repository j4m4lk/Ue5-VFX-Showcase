// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Globe/GlobeDataTypes.h"
#include "GlobeStatIcon.generated.h"

class SGlobeStatIcon;
class UTexture2D;

/**
 * Small line icon (person, building, check badge, fingerprint, ...) drawn procedurally, optionally on a soft tinted
 * disc; or a texture when Texture is set. Used by the glass stat cards; also in the Designer palette (AIGlobe).
 */
UCLASS(meta = (DisplayName = "Globe Stat Icon"))
class VFX_API UGlobeStatIcon : public UWidget
{
	GENERATED_BODY()

public:
	UGlobeStatIcon();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Icon")
	EGlobeStatIcon Icon = EGlobeStatIcon::Person;

	/** Drawn instead of the line icon when set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Icon")
	TObjectPtr<UTexture2D> Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Icon")
	FLinearColor Color = FLinearColor(0.35f, 0.8f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Icon", meta = (ClampMin = "6.0", UIMax = "96.0"))
	float Size = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Icon", meta = (ClampMin = "0.5", UIMax = "6.0"))
	float LineThickness = 1.5f;

	/** Soft disc behind the icon (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Icon", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DiscOpacity = 0.16f;

	/** Disc diameter relative to Size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Icon", meta = (ClampMin = "1.0", UIMax = "2.5"))
	float DiscScale = 1.7f;

	FVector2D ComputeSize() const;
	int32 PaintIcon(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SGlobeStatIcon> MyIcon;
};
