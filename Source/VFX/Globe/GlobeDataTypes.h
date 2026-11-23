// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GlobeDataTypes.generated.h"

class UTexture2D;

/** Small line icons drawn by the cards (no textures needed). */
UENUM(BlueprintType)
enum class EGlobeStatIcon : uint8
{
	None,
	Person,
	People,
	Building,
	CheckBadge,
	Fingerprint,
	Document,
	Globe,
	Clock,
	Chart,
};

/** One labelled stat on a card (e.g. "Visas approved" / "1,240"), with an optional icon. */
USTRUCT(BlueprintType)
struct VFX_API FGlobeDataStat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FText Label;

	/** Already formatted for display. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FText Value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	EGlobeStatIcon Icon = EGlobeStatIcon::None;

	/** Optional picture used instead of the drawn icon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	TObjectPtr<UTexture2D> IconTexture;

	/** Icon colour; alpha 0 = use the card's accent colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FLinearColor IconColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	FGlobeDataStat() = default;
	FGlobeDataStat(const FText& InLabel, const FText& InValue) : Label(InLabel), Value(InValue) {}
};

/**
 * One item shown by the globe's data-link display (one curved particle link + one stat card).
 * Supplied by the caller; the display never invents values. Items are matched by Id, so updating an item with
 * the same Id changes its card in place and keeps its particle stream running.
 */
USTRUCT(BlueprintType)
struct VFX_API FGlobeDataItem
{
	GENERATED_BODY()

	/** Card name / heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FText Title;

	/** Stable identity. Must be unique and not None (items added on the globe actor fill it from the Title). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FName Id;

	/** Small line under the title (e.g. the country or region); empty hides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FText Subtitle;

	/** Already formatted for display (e.g. "1,284", "42 ms", "98.2%"); empty hides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FText Value;

	/** Optional second line; empty hides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FText Secondary;

	/** Show a progress bar filled to Progress (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	bool bHasProgress = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bHasProgress"))
	float Progress = 0.0f;

	/** Short activity summary shown on every card under the stats; empty hides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data", meta = (MultiLine = "true"))
	FText Description;

	/** Labelled stat rows (the card shows the first few; the detail card shows all). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	TArray<FGlobeDataStat> Stats;

	/** Longer text shown only on the detail card (the single selected card in Earth mode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data", meta = (MultiLine = "true"))
	FText Details;

	FGlobeDataItem() = default;
	FGlobeDataItem(FName InId, const FText& InTitle, const FText& InValue, const FText& InSecondary = FText::GetEmpty())
		: Title(InTitle), Id(InId), Value(InValue), Secondary(InSecondary)
	{
	}
};

/**
 * Where one slot's link ends, relative to the globe as seen by the camera: an offset in the camera's screen
 * plane through the globe centre, in globe radii (X = right, Y = up). The card sits on the outer side of the
 * endpoint. Slots are filled in list order: item 1 -> slot 1, ...
 */
USTRUCT(BlueprintType)
struct VFX_API FGlobeDataSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe Data")
	FVector2D EndpointOffset = FVector2D(-1.6, 0.75);

	FGlobeDataSlot() = default;
	explicit FGlobeDataSlot(const FVector2D& InOffset) : EndpointOffset(InOffset) {}
};
