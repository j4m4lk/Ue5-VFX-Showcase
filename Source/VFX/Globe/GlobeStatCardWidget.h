// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Globe/GlobeDataTypes.h"
#include "GlobeStatCardWidget.generated.h"

class UBackgroundBlur;
class UBorder;
class UGlobeCardEdgeFlow;
class UImage;
class UProgressBar;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWidgetTree;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGlobeStatCardClicked, FName, ItemId);

/** One stat category shown on the country cards (e.g. Applicants), in display order. */
USTRUCT(BlueprintType)
struct VFX_API FGlobeCardStatCategory
{
	GENERATED_BODY()

	/** Name used in each country's Values and in the description ({Key}). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Category")
	FName Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Category")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Category")
	EGlobeStatIcon Icon = EGlobeStatIcon::None;

	/** Optional picture used instead of the drawn icon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Category")
	TObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Category")
	bool bShow = true;

	FGlobeCardStatCategory() = default;
	FGlobeCardStatCategory(FName InKey, const FText& InLabel, EGlobeStatIcon InIcon)
		: Key(InKey), Label(InLabel), Icon(InIcon)
	{
	}
};

/** The numbers for one country / place. A card whose title matches Name shows them. */
USTRUCT(BlueprintType)
struct VFX_API FGlobeCardCountryData
{
	GENERATED_BODY()

	/** Matched (ignoring case) against the data item's Title or Id, e.g. "Lebanon", "UAE". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Country")
	FText Name;

	/** Small line under the title (empty keeps the item's own subtitle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Country")
	FText Subtitle;

	/** Value per category Key (e.g. Applicants = 12000). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Country")
	TMap<FName, int64> Values;

	/** Replaces the shared Description Format for this country (same {Key} placeholders); empty = shared one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Country", meta = (MultiLine = "true"))
	FText DescriptionOverride;

	/**
	 * The country's colours (e.g. flag colours: Pakistan green + white). Used by the coloured particles swirling
	 * around the link's white stream and by the particles running around this card's edge. Empty = the built-in flag
	 * colours for common countries (by Name), if any.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Country", meta = (HideAlphaChannel))
	TArray<FLinearColor> Colors;

	/** Marks the numbers as placeholders: the card shows the Sample Badge. Untick once real numbers are entered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Country")
	bool bSampleData = true;
};

UENUM(BlueprintType)
enum class EGlobeCardStatsLayout : uint8
{
	/** Tiles: icon, value and label, Grid Columns per row. */
	Grid,
	/** One row per stat: icon, label, value on the right. */
	List,
};

/**
 * Glass stat card for one FGlobeDataItem (a city / place and its stats): frosted background blur, tinted
 * translucent rounded panel with a thin border and accent line, title + subtitle, big value, secondary line,
 * labelled stat rows, optional progress bar, and the Details text in detailed mode. Clicking it broadcasts
 * OnCardClicked (used to select the item). The globe's DataLinks component spawns one per item next to its link.
 *
 * Edit WBP_GlobeStatCard (Content/AIGlobe/UI): select the top entry in the Hierarchy and every look setting below is
 * in the Details panel ("Glass Card"); the Designer shows the first country. The named parts (CardSize, Blur, Glass,
 * AccentLine, AccentDot, TitleText, SampleBadge, SubtitleText, ValueText, SecondaryText, Divider, StatsBox,
 * ProgressLabel, ProgressBar, DescriptionText,
 * DetailsText) can be moved or restyled in the Designer; all are optional.
 */
UCLASS(Blueprintable)
class VFX_API UGlobeStatCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void SetData(const FGlobeDataItem& Item);

	/** Sets Accent Color at runtime (accent line, dot, title tint, progress fill and selection highlight). */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void SetAccentColor(FLinearColor Color);

	/** Highlighted border when this item is the selected one. */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void SetSelected(bool bInSelected);

	/** Detailed layout: larger, shows all stats and the Details text. */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void SetDetailed(bool bInDetailed);

	UFUNCTION(BlueprintPure, Category = "Globe Data")
	FName GetItemId() const { return ItemId; }

	/** Re-applies every Glass Card setting and the country data (call after changing them at runtime). */
	UFUNCTION(BlueprintCallable, Category = "Glass Card")
	void ApplyStyle();

	/** Country entry matching Title / Id (null if none). */
	const FGlobeCardCountryData* FindCountry(const FText& Title, FName Id) const;

	// ---------------- Country data (visa activity) ----------------

	/** Cards whose title matches a country below show its numbers with the categories (else the item's own data). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	bool bUseCountryData = true;

	/** Numbers per country. Edit the values here; untick Sample Data once they are real. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data", meta = (TitleProperty = "Name"))
	TArray<FGlobeCardCountryData> Countries;

	/** Stat categories in display order: key, label, icon, icon colour, shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data", meta = (TitleProperty = "Label"))
	TArray<FGlobeCardStatCategory> Categories;

	/**
	 * Activity summary under the stats. Placeholders: {Country}, any category {Key} (e.g. {Applicants}), and
	 * {ProgressPercent} (Progress Numerator / Progress Denominator).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data", meta = (MultiLine = "true"))
	FText DescriptionFormat;

	/** Big number under the title (a category Key; None hides it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	FName HeadlineKey;

	/** Line under the big number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	FText HeadlineLabel;

	/** Progress bar = Numerator / Denominator (category Keys; None hides the bar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	FName ProgressNumeratorKey = TEXT("Approved");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	FName ProgressDenominatorKey = TEXT("Applicants");

	/** Caption over the progress bar; {Percent} is replaced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	FText ProgressCaption;

	/** 12,400 -> 12.4K, 1,250,000 -> 1.3M. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	bool bCompactNumbers = false;

	/** Small tag beside the title while a country's numbers are placeholders (Sample Data ticked). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	FText SampleBadgeText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Data")
	FLinearColor SampleBadgeColor = FLinearColor(1.0f, 0.72f, 0.3f, 0.9f);

	/** The matching country's Colors (empty when the card shows no country, or it has none). */
	const TArray<FLinearColor>& GetFlowColors() const { return FlowColors; }

	// ---------------- Country flow: particles around the card's edge ----------------

	/** Particles in the country's Colors running around the card's edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow")
	bool bShowEdgeFlow = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (ClampMin = "0", ClampMax = "200"))
	int32 EdgeParticleCount = 24;

	/** Laps around the card per second (negative = the other way). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float EdgeSpeed = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (ClampMin = "0.5", UIMax = "16.0"))
	float EdgeParticleSize = 4.0f;

	/** Dots in each particle's fading trail, and the pixels between them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (ClampMin = "0", ClampMax = "30"))
	int32 EdgeTrailLength = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (ClampMin = "0.5", UIMax = "20.0"))
	float EdgeTrailSpacing = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeOpacity = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float EdgeGlow = 0.6f;

	/** Moves the path inside (+) or outside (-) the card's edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (UIMin = "-20.0", UIMax = "20.0"))
	float EdgeInset = 0.0f;

	/** 0 = evenly spaced, 1 = small groups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Country Flow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeClumping = 0.5f;

	// ---------------- Stats layout ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats")
	EGlobeCardStatsLayout StatsLayout = EGlobeCardStatsLayout::Grid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (ClampMin = "1", ClampMax = "4", EditCondition = "StatsLayout == EGlobeCardStatsLayout::Grid", EditConditionHides))
	int32 GridColumns = 2;

	/** Space between grid tiles (horizontal, vertical). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (EditCondition = "StatsLayout == EGlobeCardStatsLayout::Grid", EditConditionHides))
	FVector2D GridSpacing = FVector2D(12.0, 10.0);

	/** Value text size in grid tiles (list rows use Stat Size). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (ClampMin = "6", UIMax = "48", EditCondition = "StatsLayout == EGlobeCardStatsLayout::Grid", EditConditionHides))
	int32 TileValueSize = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (ClampMin = "6", UIMax = "32", EditCondition = "StatsLayout == EGlobeCardStatsLayout::Grid", EditConditionHides))
	int32 TileLabelSize = 9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats")
	bool bShowIcons = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (ClampMin = "6.0", UIMax = "64.0"))
	float IconSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (ClampMin = "0.5", UIMax = "6.0"))
	float IconLineThickness = 1.4f;

	/** Soft disc behind each icon (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IconDiscOpacity = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Stats", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float IconGap = 8.0f;

	UPROPERTY(BlueprintAssignable, Category = "Globe Data")
	FOnGlobeStatCardClicked OnCardClicked;

	/** Blueprint hooks (e.g. to play a highlight animation in a restyled card). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Globe Data")
	void OnDataChanged(const FGlobeDataItem& Item);

	UFUNCTION(BlueprintImplementableEvent, Category = "Globe Data")
	void OnSelectionChanged(bool bIsSelected);

	UFUNCTION(BlueprintImplementableEvent, Category = "Globe Data")
	void OnDetailModeChanged(bool bIsDetailed);

	/** Default card layout (all the named parts). Used by the plain C++ class and by the asset generator. */
	static void BuildDefaultLayout(UWidgetTree* Tree);

	// ---------------- Colours (the same in the Designer and in play) ----------------

	/**
	 * On: the Text colours below (Title Color, Value Color, ...) are applied to the text widgets.
	 * Off: each text widget keeps the Color and Opacity you set on it in the Designer (TitleText, SubtitleText,
	 * ValueText, SecondaryText, SampleBadge, ProgressLabel, DescriptionText, DetailsText).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Colors", meta = (DisplayName = "Use Card Text Colors"))
	bool bUseCardTextColors = true;

	/** Dot, top line, border (when Border Uses Accent), progress bar, title tint and selection highlight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Colors", meta = (HideAlphaChannel))
	FLinearColor AccentColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Every stat icon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Colors")
	FLinearColor IconColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// ---------------- Glass ----------------

	/** Frosted blur of what is behind the card (0 = clear glass; costs a little GPU per card). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", UIMax = "40.0"))
	float BlurStrength = 6.0f;

	/** Colour of the glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (HideAlphaChannel))
	FLinearColor GlassTint = FLinearColor(0.02f, 0.07f, 0.11f, 1.0f);

	/** How solid the glass tint is (0 = only blur, 1 = opaque). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlassOpacity = 0.42f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", UIMax = "40.0"))
	float CornerRadius = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", UIMax = "6.0"))
	float BorderThickness = 1.0f;

	/** Border uses the link's accent colour (else Border Color). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass")
	bool bBorderUsesAccent = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (HideAlphaChannel, EditCondition = "!bBorderUsesAccent"))
	FLinearColor BorderColor = FLinearColor(0.6f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BorderOpacity = 0.35f;

	/** Border opacity when the card is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SelectedBorderOpacity = 0.9f;

	/** Thin accent line along the top edge (0 hides it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", UIMax = "6.0"))
	float AccentLineThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Glass", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AccentLineOpacity = 0.85f;

	// ---------------- Layout ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout")
	FMargin CardPadding = FMargin(16.0f, 12.0f, 16.0f, 14.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout")
	FMargin DetailPadding = FMargin(22.0f, 18.0f, 22.0f, 20.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout", meta = (ClampMin = "60.0", UIMax = "800.0"))
	float CardMinWidth = 270.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout", meta = (ClampMin = "60.0", UIMax = "800.0"))
	float CardMaxWidth = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout", meta = (ClampMin = "60.0", UIMax = "1000.0"))
	float DetailMinWidth = 340.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout", meta = (ClampMin = "60.0", UIMax = "1000.0"))
	float DetailMaxWidth = 440.0f;

	/** Stat rows shown on the small card (the detail card shows all). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout", meta = (ClampMin = "0", UIMax = "12"))
	int32 MaxCompactStats = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Layout", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float StatRowSpacing = 3.0f;

	// ---------------- Text ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "48"))
	int32 TitleSize = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "32"))
	int32 SubtitleSize = 9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "72"))
	int32 ValueSize = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "96"))
	int32 DetailValueSize = 34;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "32"))
	int32 SecondarySize = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "32"))
	int32 StatSize = 11;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "32"))
	int32 DetailsSize = 11;

	/** Title takes on some of the accent colour (0 = Title Color only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TitleAccentTint = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (HideAlphaChannel))
	FLinearColor TitleColor = FLinearColor(0.92f, 0.97f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text")
	FLinearColor SubtitleColor = FLinearColor(0.55f, 0.75f, 0.88f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (HideAlphaChannel))
	FLinearColor ValueColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text")
	FLinearColor SecondaryColor = FLinearColor(0.62f, 0.74f, 0.82f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text")
	FLinearColor StatLabelColor = FLinearColor(0.58f, 0.7f, 0.8f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (HideAlphaChannel))
	FLinearColor StatValueColor = FLinearColor(0.93f, 0.97f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text")
	FLinearColor DetailsColor = FLinearColor(0.8f, 0.87f, 0.92f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "32"))
	int32 DescriptionSize = 9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text")
	FLinearColor DescriptionColor = FLinearColor(0.68f, 0.78f, 0.86f, 0.95f);

	/** Progress caption and sample badge size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "6", UIMax = "32"))
	int32 CaptionSize = 8;

	/** Subtitle in capitals with wide letter spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text")
	bool bUppercaseSubtitle = true;

	/** Soft text shadow (helps on bright backgrounds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Card|Text", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TextShadowOpacity = 0.45f;


	UGlobeStatCardWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> CardSize;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UBackgroundBlur> Blur;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Glass;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UImage> AccentLine;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UImage> AccentDot;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SecondaryText;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Divider;

	/** Filled with one row per stat at runtime. */
	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> StatsBox;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsText;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SampleBadge;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ProgressLabel;

	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	/** Particles around the card's edge (added over the card automatically when the layout has none). */
	UPROPERTY(BlueprintReadOnly, Category = "Globe Data", meta = (BindWidgetOptional))
	TObjectPtr<UGlobeCardEdgeFlow> EdgeFlow;

private:
	void BindParts();
	void RebuildStats();
	/** Data = SourceItem, filled from the matching country entry. */
	void ResolveData();
	FText FormatNumber(int64 Value) const;

	FGlobeDataItem SourceItem;
	FText ProgressCaptionText;
	bool bShowSampleBadge = false;
	TArray<FLinearColor> FlowColors;
	/** Adds EdgeFlow over the card (in CardLayers) when the layout has none. */
	void EnsureEdgeFlow();

	FGlobeDataItem Data;
	FName ItemId;
	FLinearColor Accent = FLinearColor(0.15f, 0.85f, 1.0f, 1.0f);
	bool bSelected = false;
	bool bDetailed = false;
};
