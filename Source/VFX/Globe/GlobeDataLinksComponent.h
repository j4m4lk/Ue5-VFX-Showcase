// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Globe/GlobeDataTypes.h"
#include "Globe/GlobeStatCardWidget.h"
#include "GlobeDataLinksComponent.generated.h"

class AGlobeVFXActor;
class UGlobeStatCardWidget;
class UNiagaraComponent;
class UNiagaraSystem;

/**
 * Dynamic data-link display around the globe: 0..4 items, each drawn as one curved particle stream
 * (NS_GlobeDataLink) that comes out from behind the globe and ends at a glowing node next to a UMG stat card.
 *  - Items fill the slots in list order (default: 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right).
 *  - Links are camera-relative: every frame the endpoints are placed in the camera's screen plane through the
 *    globe centre, so they stay aligned while the globe moves, scales or rotates, the camera orbits / zooms,
 *    or the viewport is resized. Only Niagara User parameters are updated; particles are never touched from C++.
 *  - Adding / removing an item fades its link and card in / out; the others glide to their new slots.
 *  - Updating an item's values only changes its card text; its stream keeps running.
 * Lives on AGlobeVFXActor (game worlds only; nothing is shown in the editor viewport).
 */
UCLASS(ClassGroup = (AIGlobe), meta = (BlueprintSpawnableComponent, DisplayName = "Globe Data Links"))
class VFX_API UGlobeDataLinksComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGlobeDataLinksComponent();

	static constexpr int32 MaxItems = 4;

	/** Replaces the displayed list. More than 4 items: the first 4 are shown and a warning is logged. Items with a
	 *  None or duplicate Id are rejected (logged). Items whose Id is already shown keep their stream. */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void SetDataItems(const TArray<FGlobeDataItem>& Items);

	/** Updates the item with the same Id in place (card only). Returns false if no such item is shown. */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	bool UpdateDataItem(const FGlobeDataItem& Item);

	/** Appends an item. Returns false (logged) when 4 items are already shown or the Id is invalid / in use. */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	bool AddDataItem(const FGlobeDataItem& Item);

	/** Removes an item by Id; the remaining items move up to fill the slots in list order. */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	bool RemoveDataItem(FName Id);

	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void ClearDataItems();

	UFUNCTION(BlueprintPure, Category = "Globe Data")
	const TArray<FGlobeDataItem>& GetDataItems() const { return Items; }

	UFUNCTION(BlueprintPure, Category = "Globe Data")
	int32 GetNumDataItems() const { return Items.Num(); }

	/** Number of link visuals currently drawn (includes ones still fading out). */
	UFUNCTION(BlueprintPure, Category = "Globe Data")
	int32 GetNumVisibleLinks() const;

	/** Master visibility of the whole display (fades). */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void SetDisplayEnabled(bool bEnabled);

	/** Selects an item (as clicking its card does): its card is highlighted and it is the one shown in detail
	 *  mode. None clears the selection. Returns false if no such item is in the list. */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	bool SelectDataItem(FName Id);

	UFUNCTION(BlueprintPure, Category = "Globe Data")
	FName GetSelectedItemId() const { return SelectedId; }

	/** Detail mode shows only the selected item (or the first item if none is selected) as one larger card with its
	 *  Details text; the other links and cards fade out. The viewer pawn turns it on while the earth is shown (S). */
	UFUNCTION(BlueprintCallable, Category = "Globe Data")
	void SetDetailMode(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Globe Data")
	bool IsDetailMode() const { return bDetailMode; }

	/** Fired when a card is clicked (after it has been selected). */
	UPROPERTY(BlueprintAssignable, Category = "Globe Data")
	FOnGlobeStatCardClicked OnDataItemClicked;

	/** Debug/inspection: world-space endpoint of the visual showing Id (false if not shown). */
	bool GetLinkEndpoint(FName Id, FVector& OutWorld, FVector2D& OutScreen) const;

	/** Clicking a card selects it (needs the mouse cursor; AGlobeViewerPawn shows it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Selection")
	bool bClickToSelect = true;

	/** Where the single detail card's link ends (camera screen plane, globe radii). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Selection")
	FGlobeDataSlot DetailSlot = FGlobeDataSlot(FVector2D(1.6, 0.35));

	/** Detail mode with nothing selected: On = show the first item; Off = keep showing all cards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Selection")
	bool bDetailFallsBackToFirstItem = true;

	/** NS_GlobeDataLink. Auto-filled when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data")
	TObjectPtr<UNiagaraSystem> LinkSystem;

	/** Stat card class: UGlobeStatCardWidget or a Widget Blueprint derived from it. Empty = WBP_GlobeStatCard (Content/AIGlobe/UI). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data")
	TSubclassOf<UGlobeStatCardWidget> CardWidgetClass;

	/** Slot endpoints in list order (camera screen plane, globe radii). Default TL, TR, BL, BR. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Layout", meta = (EditFixedSize))
	TArray<FGlobeDataSlot> Slots;

	/** Endpoints and cards are kept this far inside the viewport edges (fraction of the viewport size). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Layout", meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float SafeMargin = 0.05f;

	/** Pixels between the endpoint node and its card. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Layout", meta = (ClampMin = "0.0", UIMax = "100.0"))
	float CardGap = 18.0f;

	/** How far behind the globe centre the streams start (globe radii along the view direction). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Layout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AnchorDepth = 0.6f;

	/** How far outside the silhouette the streams come round the edge (globe radii). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Layout", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float EmergeRadius = 1.2f;

	/** Occluder radius = globe surface radius x this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Layout", meta = (ClampMin = "0.5", ClampMax = "1.5"))
	float OccluderScale = 1.0f;

	/** Seconds for a link and card to fade in / out, and for links to glide to a new slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.05", UIMax = "3.0", Units = "s"))
	float RevealTime = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.05", UIMax = "3.0", Units = "s"))
	float SlotMoveTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (HideAlphaChannel))
	FLinearColor LinkColor = FLinearColor(0.15f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float LinkIntensity = 2.5f;

	/** Stream particles per second and seconds to travel from the globe to the node. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.0", UIMax = "400.0"))
	float StreamRate = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.2", UIMax = "10.0", Units = "s"))
	float StreamTravelTime = 2.4f;

	/** Sprite sizes and stream width as fractions of the globe radius (scale with the globe). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float StreamSizeFactor = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float StreamWidthFactor = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.0", UIMax = "200.0"))
	float FilamentRate = 36.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float FilamentSpreadFactor = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Look", meta = (ClampMin = "0.01", ClampMax = "0.3"))
	float NodeSizeFactor = 0.08f;

	/**
	 * Country colour flow: a second particle layer in the card's country Colors (set per country in WBP_GlobeStatCard)
	 * swirling around the white stream. Links whose card has no country colours show only the white stream.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Country Colors")
	bool bShowCountryColors = true;

	/** Coloured particles per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Country Colors", meta = (ClampMin = "0.0", UIMax = "400.0"))
	float CountryParticleRate = 140.0f;

	/** Particle size and swirl radius around the white core, as fractions of the globe radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Country Colors", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float CountrySizeFactor = 0.024f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Country Colors", meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float CountrySpreadFactor = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Country Colors", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float CountryIntensity = 2.2f;

	/** Travel time from the globe to the card relative to the white stream (1 = same speed, 2 = half as fast). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Country Colors", meta = (ClampMin = "0.25", UIMax = "4.0"))
	float CountryTravelScale = 1.15f;

	// ---------------- The link joins the card ----------------

	/**
	 * The link ends at the card's inner edge (the side facing the globe) and its glowing node sits behind the card's
	 * centre, so the stream runs into the card. Off: the link ends at a node beside the card (the old look).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop")
	bool bLinkEndsAtCard = true;

	/** Brightness of the node glow while it sits behind the card (1 = full, 0 = hidden). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bLinkEndsAtCard"))
	float NodeBehindCardIntensity = 0.3f;

	/** Size of the node glow behind the card, relative to Node Size Factor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.0", UIMax = "2.0", EditCondition = "bLinkEndsAtCard"))
	float NodeBehindCardSize = 0.7f;

	/** Particles that leave the link's end and run around the card (a rounded rectangle the card's size), both ways. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (EditCondition = "bLinkEndsAtCard"))
	bool bShowCardLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.0", UIMax = "400.0", EditCondition = "bLinkEndsAtCard"))
	float LoopParticleRate = 110.0f;

	/** Seconds for a particle to get from the link's end round to the far side of the card. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.1", UIMax = "10.0", Units = "s", EditCondition = "bLinkEndsAtCard"))
	float LoopTravelTime = 1.6f;

	/** How far round each particle goes (1 = to the far side, where both directions meet; 0.5 = halfway). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.1", ClampMax = "2.0", EditCondition = "bLinkEndsAtCard"))
	float LoopTravel = 1.0f;

	/** Particle size and jitter around the path, as fractions of the globe radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.001", ClampMax = "0.1", EditCondition = "bLinkEndsAtCard"))
	float LoopSizeFactor = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.0", ClampMax = "0.05", EditCondition = "bLinkEndsAtCard"))
	float LoopJitterFactor = 0.006f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "0.0", UIMax = "4.0", EditCondition = "bLinkEndsAtCard"))
	float LoopIntensity = 1.6f;

	/** Corner shape: 2 = ellipse, ~6-10 = rounded rectangle like the card, higher = sharper corners. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (ClampMin = "2.0", UIMax = "20.0", EditCondition = "bLinkEndsAtCard"))
	float LoopRoundness = 8.0f;

	/** Pixels the loop sits outside the card's edge (negative = inside). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Card Loop", meta = (UIMin = "-20.0", UIMax = "40.0", EditCondition = "bLinkEndsAtCard"))
	float LoopMarginPixels = 3.0f;

	/** Maximum number of Niagara link components (4 active + ones still fading out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Data|Performance", meta = (ClampMin = "4", ClampMax = "8"))
	int32 MaxLinkVisuals = 6;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** One pooled link: its Niagara component and card, plus animation state. */
	struct FLinkVisual
	{
		TWeakObjectPtr<UNiagaraComponent> Niagara;
		TWeakObjectPtr<UGlobeStatCardWidget> Card;
		FName ItemId;         // None when free
		bool bWanted = false; // false: fading out
		float Alpha = 0.0f;
		FVector2D Offset = FVector2D::ZeroVector; // current slot offset (animated)
		FVector2D TargetOffset = FVector2D::ZeroVector;
		bool bHasOffset = false;
		FVector Endpoint = FVector::ZeroVector;
		FVector2D ScreenEndpoint = FVector2D::ZeroVector;
		// The card as placed this frame (viewport pixels) and as a rectangle in the camera plane through the globe
		// centre (offsets from the centre): the link ends at its inner edge, the loop runs around it.
		bool bCardPlaced = false;
		FVector2D CardTopLeft = FVector2D::ZeroVector;
		FVector2D CardPixels = FVector2D::ZeroVector;
		FVector CardCenter = FVector::ZeroVector;
		FVector CardHalfRight = FVector::ZeroVector;
		FVector CardHalfUp = FVector::ZeroVector;
		bool bCardLeftOfGlobe = true;
	};

	UFUNCTION()
	void HandleCardClicked(FName ItemId);

	/** Id shown in detail mode, or None when all items are shown. */
	FName GetDetailItemId() const;

	bool ValidateNewItem(const FGlobeDataItem& Item, const TCHAR* Context) const;
	void SyncVisuals();
	FLinkVisual* FindVisual(FName Id);
	const FLinkVisual* FindVisual(FName Id) const;
	FLinkVisual* AcquireVisual();
	void EnsureAssets();
	void UpdateVisuals(float DeltaTime);
	void PushLinkParameters(FLinkVisual& Visual, const FVector& Center, float Radius, const FVector& Forward, const FVector& Right, const FVector& Up,
		const FVector2D& PlaneHalfExtent);
	void PlaceCard(FLinkVisual& Visual, const FVector2D& ViewportSize, float DPIScale);
	AGlobeVFXActor* GetGlobe() const;

	UPROPERTY(Transient)
	TArray<FGlobeDataItem> Items;

	/** Keeps the pooled Niagara components and cards alive (the structs above hold weak pointers). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> OwnedObjects;

	TArray<FLinkVisual> Visuals;
	FName SelectedId;
	bool bDetailMode = false;
	bool bDisplayEnabled = true;
	float DisplayAlpha = 1.0f;
	bool bStarted = false;
};
