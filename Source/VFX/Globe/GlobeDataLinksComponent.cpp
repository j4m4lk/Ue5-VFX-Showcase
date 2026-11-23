// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeDataLinksComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Globe/GlobeStatCardWidget.h"
#include "Globe/GlobeVFXActor.h"
#include "Globe/GlobeVFXParameters.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeDataLinks, Log, All);

namespace
{
	FName LinkParam(const TCHAR* Name) { return FName(Name); }

	/** Camera used for the layout. Reads the view target's camera component directly (this component ticks
	 *  after the viewer pawn), so links and cards never lag a frame behind orbit / zoom. */
	bool GetViewCamera(const APlayerController* PC, FVector& OutLocation, FQuat& OutRotation, float& OutHorizontalFOV)
	{
		if (!PC)
		{
			return false;
		}
		if (const AActor* Target = PC->GetViewTarget())
		{
			if (const UCameraComponent* Camera = Target->FindComponentByClass<UCameraComponent>())
			{
				if (Camera->IsActive())
				{
					OutLocation = Camera->GetComponentLocation();
					OutRotation = Camera->GetComponentQuat();
					OutHorizontalFOV = Camera->FieldOfView;
					return true;
				}
			}
		}
		if (const APlayerCameraManager* Manager = PC->PlayerCameraManager)
		{
			OutLocation = Manager->GetCameraLocation();
			OutRotation = Manager->GetCameraRotation().Quaternion();
			OutHorizontalFOV = Manager->GetFOVAngle();
			return true;
		}
		return false;
	}
}

UGlobeDataLinksComponent::UGlobeDataLinksComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the viewer pawn has moved its camera this frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	Slots = {
		FGlobeDataSlot(FVector2D(-1.65, 0.8)),  // 1: top left
		FGlobeDataSlot(FVector2D(1.65, 0.8)),   // 2: top right
		FGlobeDataSlot(FVector2D(-1.65, -0.8)), // 3: bottom left
		FGlobeDataSlot(FVector2D(1.65, -0.8)),  // 4: bottom right
	};
}

AGlobeVFXActor* UGlobeDataLinksComponent::GetGlobe() const
{
	return Cast<AGlobeVFXActor>(GetOwner());
}

void UGlobeDataLinksComponent::BeginPlay()
{
	Super::BeginPlay();
	bStarted = true;
	EnsureAssets();
	SyncVisuals();
}

void UGlobeDataLinksComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FLinkVisual& Visual : Visuals)
	{
		if (UGlobeStatCardWidget* Card = Visual.Card.Get())
		{
			Card->RemoveFromParent();
		}
		if (UNiagaraComponent* Niagara = Visual.Niagara.Get())
		{
			Niagara->DestroyComponent();
		}
	}
	Visuals.Reset();
	OwnedObjects.Reset();
	bStarted = false;
	Super::EndPlay(EndPlayReason);
}

void UGlobeDataLinksComponent::EnsureAssets()
{
	if (!LinkSystem)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), GlobeVFX::Assets::NiagaraFolder, GlobeVFX::Assets::DataLinkSystemName, GlobeVFX::Assets::DataLinkSystemName);
		LinkSystem = LoadObject<UNiagaraSystem>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UE_CLOG(!LinkSystem, LogGlobeDataLinks, Warning, TEXT("%s not found: data links draw cards only. Run AIGlobe.GenerateMaterials."), *Path);
	}
	// Empty or the plain C++ class (older levels saved it here): use the editable glass card Widget Blueprint, so
	// what is set in WBP_GlobeStatCard is what plays. The C++ class only stays as a fallback when it is missing.
	if (!CardWidgetClass || CardWidgetClass == UGlobeStatCardWidget::StaticClass())
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s_C"), GlobeVFX::Assets::UIFolder, GlobeVFX::Assets::StatCardWidgetName, GlobeVFX::Assets::StatCardWidgetName);
		CardWidgetClass = LoadClass<UGlobeStatCardWidget>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UE_CLOG(!CardWidgetClass, LogGlobeDataLinks, Warning, TEXT("%s not found: using the built-in card layout. Run AIGlobe.GenerateWidgets."), *Path);
		if (!CardWidgetClass)
		{
			CardWidgetClass = UGlobeStatCardWidget::StaticClass();
		}
	}
	UE_LOG(LogGlobeDataLinks, Display, TEXT("Data cards use %s"), *CardWidgetClass->GetPathName());
}

// ------------------------------------------------------------------ public API

bool UGlobeDataLinksComponent::ValidateNewItem(const FGlobeDataItem& Item, const TCHAR* Context) const
{
	if (Item.Id.IsNone())
	{
		UE_LOG(LogGlobeDataLinks, Warning, TEXT("%s: rejected an item without an Id (title '%s')."), Context, *Item.Title.ToString());
		return false;
	}
	return true;
}

void UGlobeDataLinksComponent::SetDataItems(const TArray<FGlobeDataItem>& NewItems)
{
	TArray<FGlobeDataItem> Accepted;
	for (const FGlobeDataItem& Item : NewItems)
	{
		if (!ValidateNewItem(Item, TEXT("SetDataItems")))
		{
			continue;
		}
		if (Accepted.ContainsByPredicate([&Item](const FGlobeDataItem& Other) { return Other.Id == Item.Id; }))
		{
			UE_LOG(LogGlobeDataLinks, Warning, TEXT("SetDataItems: duplicate Id '%s' rejected."), *Item.Id.ToString());
			continue;
		}
		if (Accepted.Num() == MaxItems)
		{
			UE_LOG(LogGlobeDataLinks, Warning, TEXT("SetDataItems: %d valid items supplied, only the first %d are shown."), NewItems.Num(), MaxItems);
			break;
		}
		Accepted.Add(Item);
	}
	Items = MoveTemp(Accepted);
	SyncVisuals();
}

bool UGlobeDataLinksComponent::UpdateDataItem(const FGlobeDataItem& Item)
{
	FGlobeDataItem* Existing = Items.FindByPredicate([&Item](const FGlobeDataItem& Other) { return Other.Id == Item.Id; });
	if (!Existing)
	{
		UE_LOG(LogGlobeDataLinks, Warning, TEXT("UpdateDataItem: no item with Id '%s' is shown."), *Item.Id.ToString());
		return false;
	}
	*Existing = Item;
	if (FLinkVisual* Visual = FindVisual(Item.Id))
	{
		if (UGlobeStatCardWidget* Card = Visual->Card.Get())
		{
			Card->SetData(Item); // text only: the link's Niagara component is not touched
		}
	}
	return true;
}

bool UGlobeDataLinksComponent::AddDataItem(const FGlobeDataItem& Item)
{
	if (!ValidateNewItem(Item, TEXT("AddDataItem")))
	{
		return false;
	}
	if (Items.ContainsByPredicate([&Item](const FGlobeDataItem& Other) { return Other.Id == Item.Id; }))
	{
		UE_LOG(LogGlobeDataLinks, Warning, TEXT("AddDataItem: Id '%s' is already shown (use UpdateDataItem)."), *Item.Id.ToString());
		return false;
	}
	if (Items.Num() >= MaxItems)
	{
		UE_LOG(LogGlobeDataLinks, Warning, TEXT("AddDataItem: already showing %d items; '%s' rejected."), MaxItems, *Item.Id.ToString());
		return false;
	}
	Items.Add(Item);
	SyncVisuals();
	return true;
}

bool UGlobeDataLinksComponent::RemoveDataItem(FName Id)
{
	const int32 Removed = Items.RemoveAll([Id](const FGlobeDataItem& Other) { return Other.Id == Id; });
	if (Removed > 0)
	{
		SyncVisuals();
	}
	return Removed > 0;
}

void UGlobeDataLinksComponent::ClearDataItems()
{
	Items.Reset();
	SyncVisuals();
}

void UGlobeDataLinksComponent::SetDisplayEnabled(bool bEnabled)
{
	bDisplayEnabled = bEnabled;
}

int32 UGlobeDataLinksComponent::GetNumVisibleLinks() const
{
	int32 Count = 0;
	for (const FLinkVisual& Visual : Visuals)
	{
		Count += !Visual.ItemId.IsNone() && Visual.Alpha > 0.0f ? 1 : 0;
	}
	return Count;
}

bool UGlobeDataLinksComponent::GetLinkEndpoint(FName Id, FVector& OutWorld, FVector2D& OutScreen) const
{
	if (const FLinkVisual* Visual = FindVisual(Id))
	{
		OutWorld = Visual->Endpoint;
		OutScreen = Visual->ScreenEndpoint;
		return true;
	}
	return false;
}

// ------------------------------------------------------------------ visuals

UGlobeDataLinksComponent::FLinkVisual* UGlobeDataLinksComponent::FindVisual(FName Id)
{
	return Visuals.FindByPredicate([Id](const FLinkVisual& Visual) { return Visual.ItemId == Id; });
}

const UGlobeDataLinksComponent::FLinkVisual* UGlobeDataLinksComponent::FindVisual(FName Id) const
{
	return Visuals.FindByPredicate([Id](const FLinkVisual& Visual) { return Visual.ItemId == Id; });
}

UGlobeDataLinksComponent::FLinkVisual* UGlobeDataLinksComponent::AcquireVisual()
{
	for (FLinkVisual& Visual : Visuals)
	{
		if (Visual.ItemId.IsNone())
		{
			return &Visual;
		}
	}
	if (Visuals.Num() >= MaxLinkVisuals)
	{
		// Pool exhausted by links still fading out: reuse the most faded one.
		FLinkVisual* Oldest = nullptr;
		for (FLinkVisual& Visual : Visuals)
		{
			if (!Visual.bWanted && (!Oldest || Visual.Alpha < Oldest->Alpha))
			{
				Oldest = &Visual;
			}
		}
		if (Oldest)
		{
			Oldest->ItemId = NAME_None;
			Oldest->Alpha = 0.0f;
			Oldest->bHasOffset = false;
			if (UNiagaraComponent* Niagara = Oldest->Niagara.Get())
			{
				Niagara->DeactivateImmediate();
			}
		}
		return Oldest;
	}

	AActor* Owner = GetOwner();
	FLinkVisual& Visual = Visuals.AddDefaulted_GetRef();
	if (LinkSystem && Owner)
	{
		UNiagaraComponent* Niagara = NewObject<UNiagaraComponent>(Owner, MakeUniqueObjectName(Owner, UNiagaraComponent::StaticClass(), TEXT("DataLink")));
		Niagara->bAutoActivate = false;
		Niagara->SetAsset(LinkSystem);
		Niagara->SetCastShadow(false);
		Niagara->SetUsingAbsoluteLocation(true);
		Niagara->SetUsingAbsoluteRotation(true); // world axes: C++ passes camera-relative offsets in world space
		Niagara->SetUsingAbsoluteScale(true);
		Niagara->TranslucencySortPriority = 3;   // over the hologram shell (1) and the surface points (2)
		Niagara->SetupAttachment(Owner->GetRootComponent());
		Niagara->RegisterComponent();
		OwnedObjects.Add(Niagara);
		Visual.Niagara = Niagara;
	}
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (UGlobeStatCardWidget* Card = CreateWidget<UGlobeStatCardWidget>(PC, CardWidgetClass ? *CardWidgetClass : UGlobeStatCardWidget::StaticClass()))
		{
			Card->AddToViewport(10);
			Card->OnCardClicked.AddDynamic(this, &UGlobeDataLinksComponent::HandleCardClicked);
			Card->SetRenderOpacity(0.0f);
			Card->SetVisibility(ESlateVisibility::Collapsed);
			OwnedObjects.Add(Card);
			Visual.Card = Card;
		}
	}
	return &Visual;
}

FName UGlobeDataLinksComponent::GetDetailItemId() const
{
	if (!bDetailMode || Items.Num() == 0)
	{
		return NAME_None;
	}
	if (!SelectedId.IsNone())
	{
		return SelectedId;
	}
	return bDetailFallsBackToFirstItem ? Items[0].Id : NAME_None;
}

bool UGlobeDataLinksComponent::SelectDataItem(FName Id)
{
	if (!Id.IsNone() && !Items.ContainsByPredicate([Id](const FGlobeDataItem& Item) { return Item.Id == Id; }))
	{
		UE_LOG(LogGlobeDataLinks, Warning, TEXT("SelectDataItem: no item with Id '%s'."), *Id.ToString());
		return false;
	}
	if (SelectedId != Id)
	{
		SelectedId = Id;
		SyncVisuals(); // highlight, and in detail mode switch the shown card
	}
	return true;
}

void UGlobeDataLinksComponent::SetDetailMode(bool bEnabled)
{
	if (bDetailMode != bEnabled)
	{
		bDetailMode = bEnabled;
		SyncVisuals();
	}
}

void UGlobeDataLinksComponent::HandleCardClicked(FName ItemId)
{
	if (!bClickToSelect || ItemId.IsNone())
	{
		return;
	}
	SelectDataItem(ItemId);
	OnDataItemClicked.Broadcast(ItemId);
}

void UGlobeDataLinksComponent::SyncVisuals()
{
	if (!SelectedId.IsNone() && !Items.ContainsByPredicate([this](const FGlobeDataItem& Item) { return Item.Id == SelectedId; }))
	{
		SelectedId = NAME_None; // the selected item was removed
	}
	if (!bStarted)
	{
		return; // BeginPlay syncs; items set earlier are kept
	}
	for (FLinkVisual& Visual : Visuals)
	{
		Visual.bWanted = false;
	}

	// Detail mode shows one item at DetailSlot; otherwise every item at its slot in list order.
	const FName DetailId = GetDetailItemId();
	int32 SlotIndex = 0;
	for (const FGlobeDataItem& Item : Items)
	{
		const bool bIsDetail = !DetailId.IsNone();
		if (bIsDetail && Item.Id != DetailId)
		{
			continue;
		}
		FLinkVisual* Visual = FindVisual(Item.Id);
		if (!Visual)
		{
			Visual = AcquireVisual();
			if (!Visual)
			{
				UE_LOG(LogGlobeDataLinks, Warning, TEXT("No free link visual for '%s'."), *Item.Id.ToString());
				continue;
			}
			Visual->ItemId = Item.Id;
			Visual->Alpha = 0.0f;
			Visual->bHasOffset = false;
			if (UNiagaraComponent* Niagara = Visual->Niagara.Get())
			{
				Niagara->Activate(true);
			}
		}
		Visual->bWanted = true;
		const FVector2D SlotOffset = bIsDetail ? DetailSlot.EndpointOffset
			: (Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].EndpointOffset : FGlobeDataSlot().EndpointOffset);
		++SlotIndex;
		Visual->TargetOffset = SlotOffset;
		if (!Visual->bHasOffset)
		{
			Visual->Offset = SlotOffset; // new links appear at their slot; existing ones glide there
			Visual->bHasOffset = true;
		}
		if (UGlobeStatCardWidget* Card = Visual->Card.Get())
		{
			Card->SetData(Item);
			Card->SetSelected(Item.Id == SelectedId);
			Card->SetDetailed(bIsDetail);
		}
	}
}

void UGlobeDataLinksComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateVisuals(DeltaTime);
}

void UGlobeDataLinksComponent::UpdateVisuals(float DeltaTime)
{
	DisplayAlpha = FMath::FInterpConstantTo(DisplayAlpha, bDisplayEnabled ? 1.0f : 0.0f, DeltaTime, 1.0f / FMath::Max(RevealTime, 0.01f));
	if (Visuals.Num() == 0)
	{
		return;
	}

	const AGlobeVFXActor* Globe = GetGlobe();
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	FVector CameraLocation;
	FQuat CameraRotation;
	float HorizontalFOV = 90.0f;
	FVector2D ViewportSize = FVector2D::ZeroVector;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	if (!Globe || !GetViewCamera(PC, CameraLocation, CameraRotation, HorizontalFOV) || ViewportSize.X < 1.0 || ViewportSize.Y < 1.0)
	{
		return;
	}

	const FVector Center = Globe->GetGlobeCenter();
	const float Radius = FMath::Max(Globe->GetGlobeSurfaceRadius(), 1.0f);
	const FVector Forward = CameraRotation.GetForwardVector();
	const FVector Right = CameraRotation.GetRightVector();
	const FVector Up = CameraRotation.GetUpVector();

	// Projection scale (1 / tan of the half FOV per axis) and view rect from the engine's own projection, so the
	// layout matches the render for any aspect ratio / axis constraint. Orientation comes from this frame's
	// camera (the engine's view data is last frame's), so links and cards do not lag during orbit / zoom.
	FVector2D ProjScale(1.0 / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(HorizontalFOV, 5.0f, 170.0f) * 0.5f)), 0.0);
	ProjScale.Y = ProjScale.X * ViewportSize.X / ViewportSize.Y;
	FVector2D RectMin = FVector2D::ZeroVector;
	FVector2D RectSize = ViewportSize;
	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	FSceneViewProjectionData Projection;
	if (LocalPlayer && LocalPlayer->ViewportClient && LocalPlayer->ViewportClient->Viewport
		&& LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, Projection))
	{
		const FMatrix& M = Projection.ProjectionMatrix;
		if (M.M[0][0] > 0.0 && M.M[1][1] > 0.0)
		{
			ProjScale = FVector2D(M.M[0][0], M.M[1][1]);
		}
		const FIntRect Rect = Projection.GetConstrainedViewRect();
		if (Rect.Width() > 0 && Rect.Height() > 0)
		{
			RectMin = FVector2D(Rect.Min);
			RectSize = FVector2D(Rect.Width(), Rect.Height());
		}
	}

	// Visible half extent of the screen plane through the globe centre, so endpoints stay inside the safe area.
	const float Depth = FMath::Max(FVector::DotProduct(Center - CameraLocation, Forward), Radius * 0.1f);
	const FVector2D PlaneHalf(Depth / ProjScale.X, Depth / ProjScale.Y);
	const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);

	const float FadeSpeed = 1.0f / FMath::Max(RevealTime, 0.01f);
	const float MoveBlend = 1.0f - FMath::Exp(-DeltaTime * 4.0f / FMath::Max(SlotMoveTime, 0.01f));

	for (FLinkVisual& Visual : Visuals)
	{
		if (Visual.ItemId.IsNone())
		{
			continue;
		}
		Visual.Alpha = FMath::FInterpConstantTo(Visual.Alpha, Visual.bWanted ? 1.0f : 0.0f, DeltaTime, FadeSpeed);
		Visual.Offset = FMath::Lerp(Visual.Offset, Visual.TargetOffset, MoveBlend);

		if (!Visual.bWanted && Visual.Alpha <= 0.0f)
		{
			// Fully hidden: release the slot. The stream stops; nothing is left on screen.
			if (UNiagaraComponent* Niagara = Visual.Niagara.Get())
			{
				Niagara->DeactivateImmediate();
			}
			if (UGlobeStatCardWidget* Card = Visual.Card.Get())
			{
				Card->SetVisibility(ESlateVisibility::Collapsed);
			}
			Visual.ItemId = NAME_None;
			Visual.bHasOffset = false;
			continue;
		}

		// Card size in pixels (desired size is in slate units), used to leave room for the card beside the node.
		FVector2D CardPixels(230.0f * DPIScale, 90.0f * DPIScale);
		if (const UGlobeStatCardWidget* Card = Visual.Card.Get())
		{
			const FVector2D Desired = Card->GetDesiredSize();
			if (Desired.X > 1.0 && Desired.Y > 1.0)
			{
				CardPixels = Desired * DPIScale;
			}
		}
		const float MaxNdcX = FMath::Max(0.05f, 1.0f - 2.0f * SafeMargin - 2.0f * (CardPixels.X + CardGap * DPIScale) / RectSize.X);
		const float MaxNdcY = FMath::Max(0.05f, 1.0f - 2.0f * SafeMargin - CardPixels.Y / RectSize.Y);

		const float EndX = FMath::Clamp(float(Visual.Offset.X) * Radius, -MaxNdcX * PlaneHalf.X, MaxNdcX * PlaneHalf.X);
		const float EndY = FMath::Clamp(float(Visual.Offset.Y) * Radius, -MaxNdcY * PlaneHalf.Y, MaxNdcY * PlaneHalf.Y);
		const FVector EndLocal = Right * EndX + Up * EndY;
		Visual.Endpoint = Center + EndLocal;

		// Screen position (viewport pixels) of the endpoint, projected with this frame's camera.
		Visual.ScreenEndpoint = RectMin + FVector2D(
			(0.5 + 0.5 * EndX / PlaneHalf.X) * RectSize.X,
			(0.5 - 0.5 * EndY / PlaneHalf.Y) * RectSize.Y);

		// Card first (beside the slot endpoint), then the link is made to end at the card.
		PlaceCard(Visual, ViewportSize, DPIScale);
		if (bLinkEndsAtCard && Visual.bCardPlaced)
		{
			// Viewport pixels -> the camera plane through the globe centre (offsets from the centre).
			auto ToPlane = [&](const FVector2D& Pixel)
			{
				const float X = (2.0f * float((Pixel.X - RectMin.X) / RectSize.X) - 1.0f) * float(PlaneHalf.X);
				const float Y = (1.0f - 2.0f * float((Pixel.Y - RectMin.Y) / RectSize.Y)) * float(PlaneHalf.Y);
				return Right * X + Up * Y;
			};
			const FVector2D Margin(LoopMarginPixels * DPIScale, LoopMarginPixels * DPIScale);
			const FVector Min = ToPlane(Visual.CardTopLeft - Margin);
			const FVector Max = ToPlane(Visual.CardTopLeft + Visual.CardPixels + Margin);
			Visual.CardCenter = 0.5f * (Min + Max);
			Visual.CardHalfRight = Right * (0.5f * FVector::DotProduct(Max - Min, Right));
			Visual.CardHalfUp = Up * (0.5f * FMath::Abs(FVector::DotProduct(Max - Min, Up)));
			Visual.bCardLeftOfGlobe = FVector::DotProduct(Visual.CardCenter, Right) < 0.0f;
			// The link arrives at the middle of the card's inner edge (the side facing the globe).
			Visual.Endpoint = Center + Visual.CardCenter + (Visual.bCardLeftOfGlobe ? Visual.CardHalfRight : -Visual.CardHalfRight);
		}
		PushLinkParameters(Visual, Center, Radius, Forward, Right, Up, PlaneHalf);
	}
}

void UGlobeDataLinksComponent::PushLinkParameters(FLinkVisual& Visual, const FVector& Center, float Radius, const FVector& Forward, const FVector& Right, const FVector& Up,
	const FVector2D& PlaneHalfExtent)
{
	UNiagaraComponent* Niagara = Visual.Niagara.Get();
	if (!Niagara)
	{
		return;
	}
	using namespace GlobeVFX;

	// Component at the globe centre with world axes: the curve is given as offsets from the centre.
	Niagara->SetWorldLocationAndRotation(Center, FQuat::Identity);

	const FVector End = Visual.Endpoint - Center;
	const FVector2D PlaneDir = FVector2D(FVector::DotProduct(End, Right), FVector::DotProduct(End, Up)).GetSafeNormal();
	const FVector Outward = Right * PlaneDir.X + Up * PlaneDir.Y;
	const float SideX = FVector::DotProduct(End, Right) < 0.0f ? -1.0f : 1.0f;

	// Behind the globe -> round its edge just outside the silhouette -> across to the node, arriving sideways.
	const FVector Anchor = Forward * (AnchorDepth * Radius) + Outward * (0.3f * Radius);
	const FVector EmergeHandle = Forward * (0.25f * Radius) + Outward * (EmergeRadius * 1.05f * Radius);
	const FVector ArriveHandle = End - Right * (SideX * 0.6f * Radius);

	const float Fade = FMath::SmoothStep(0.0f, 1.0f, Visual.Alpha) * DisplayAlpha;
	Niagara->SetVariableVec3(LinkParam(LinkParams::P0), Anchor);
	Niagara->SetVariableVec3(LinkParam(LinkParams::P1), EmergeHandle);
	Niagara->SetVariableVec3(LinkParam(LinkParams::P2), ArriveHandle);
	Niagara->SetVariableVec3(LinkParam(LinkParams::P3), End);
	Niagara->SetVariableLinearColor(LinkParam(LinkParams::LinkColor), LinkColor);
	Niagara->SetVariableFloat(LinkParam(LinkParams::LinkIntensity), LinkIntensity * Fade);
	Niagara->SetVariableFloat(LinkParam(LinkParams::OccluderRadius), Radius * OccluderScale);
	Niagara->SetVariableFloat(LinkParam(LinkParams::StreamRate), StreamRate);
	Niagara->SetVariableFloat(LinkParam(LinkParams::StreamLifetime), StreamTravelTime);
	Niagara->SetVariableFloat(LinkParam(LinkParams::StreamSize), Radius * StreamSizeFactor);
	Niagara->SetVariableFloat(LinkParam(LinkParams::StreamWidth), Radius * StreamWidthFactor);
	Niagara->SetVariableFloat(LinkParam(LinkParams::FilamentRate), FilamentRate);
	Niagara->SetVariableFloat(LinkParam(LinkParams::FilamentLifetime), StreamTravelTime * 2.5f);
	Niagara->SetVariableFloat(LinkParam(LinkParams::FilamentSize), Radius * StreamSizeFactor * 0.6f);
	Niagara->SetVariableFloat(LinkParam(LinkParams::FilamentSpread), Radius * FilamentSpreadFactor);

	// Node behind the card's centre, and the loop of particles around the card from the link's end.
	const bool bAtCard = bLinkEndsAtCard && Visual.bCardPlaced;
	Niagara->SetVariableVec3(LinkParam(LinkParams::NodePosition), bAtCard ? Visual.CardCenter : End);
	Niagara->SetVariableFloat(LinkParam(LinkParams::NodeSize), Radius * NodeSizeFactor * (bAtCard ? NodeBehindCardSize : 1.0f));
	Niagara->SetVariableFloat(LinkParam(LinkParams::NodeIntensity), bAtCard ? NodeBehindCardIntensity : 1.0f);
	Niagara->SetVariableFloat(LinkParam(LinkParams::LoopRate), bAtCard && bShowCardLoop ? LoopParticleRate : 0.0f);
	if (bAtCard)
	{
		Niagara->SetVariableVec3(LinkParam(LinkParams::CardCenter), Visual.CardCenter);
		Niagara->SetVariableVec3(LinkParam(LinkParams::CardHalfRight), Visual.CardHalfRight);
		Niagara->SetVariableVec3(LinkParam(LinkParams::CardHalfUp), Visual.CardHalfUp);
		Niagara->SetVariableFloat(LinkParam(LinkParams::LoopStartAngle), Visual.bCardLeftOfGlobe ? 0.0f : UE_PI); // the inner edge
		Niagara->SetVariableFloat(LinkParam(LinkParams::LoopLifetime), LoopTravelTime);
		Niagara->SetVariableFloat(LinkParam(LinkParams::LoopTravel), LoopTravel);
		Niagara->SetVariableFloat(LinkParam(LinkParams::LoopSize), Radius * LoopSizeFactor);
		Niagara->SetVariableFloat(LinkParam(LinkParams::LoopJitter), Radius * LoopJitterFactor);
		Niagara->SetVariableFloat(LinkParam(LinkParams::LoopIntensity), LoopIntensity);
		Niagara->SetVariableFloat(LinkParam(LinkParams::LoopRoundness), LoopRoundness);
	}

	// Country colour flow around the white stream, in the card's country colours (up to three).
	const UGlobeStatCardWidget* Card = Visual.Card.Get();
	const TArray<FLinearColor> NoColors;
	const TArray<FLinearColor>& CountryColors = (bShowCountryColors && Card) ? Card->GetFlowColors() : NoColors;
	const int32 ColorCount = FMath::Min(CountryColors.Num(), 3);
	Niagara->SetVariableFloat(LinkParam(LinkParams::CountryRate), ColorCount > 0 ? CountryParticleRate : 0.0f);
	Niagara->SetVariableFloat(LinkParam(LinkParams::CountryColorCount), (float)ColorCount); // 0: the card loop is the link colour only
	if (ColorCount > 0)
	{
		const TCHAR* ColorParams[3] = { LinkParams::CountryColorA, LinkParams::CountryColorB, LinkParams::CountryColorC };
		for (int32 i = 0; i < 3; ++i)
		{
			FLinearColor Color = CountryColors[FMath::Min(i, ColorCount - 1)];
			Color.A = 1.0f;
			Niagara->SetVariableLinearColor(LinkParam(ColorParams[i]), Color);
		}
		Niagara->SetVariableFloat(LinkParam(LinkParams::CountryLifetime), StreamTravelTime * CountryTravelScale);
		Niagara->SetVariableFloat(LinkParam(LinkParams::CountrySize), Radius * CountrySizeFactor);
		Niagara->SetVariableFloat(LinkParam(LinkParams::CountrySpread), Radius * CountrySpreadFactor);
		Niagara->SetVariableFloat(LinkParam(LinkParams::CountryIntensity), CountryIntensity);
	}

	// Bounds: the whole curve lies within the endpoint distance plus the emerge radius.
	const float CardReach = bAtCard ? float(Visual.CardCenter.Size() + Visual.CardHalfRight.Size() + Visual.CardHalfUp.Size()) : 0.0f;
	const float Extent = FMath::Max3(float(End.Size()), EmergeRadius * 1.1f * Radius, CardReach) + Radius * 0.5f;
	Niagara->SetSystemFixedBounds(FBox(FVector(-Extent), FVector(Extent)));
}

void UGlobeDataLinksComponent::PlaceCard(FLinkVisual& Visual, const FVector2D& ViewportSize, float DPIScale)
{
	Visual.bCardPlaced = false;
	UGlobeStatCardWidget* Card = Visual.Card.Get();
	if (!Card)
	{
		return;
	}
	const float Opacity = FMath::SmoothStep(0.0f, 1.0f, Visual.Alpha) * DisplayAlpha;
	if (Opacity <= 0.001f)
	{
		Card->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// Clickable only while it is (or is becoming) a shown item; fading-out cards let clicks through.
	Card->SetVisibility(bClickToSelect && Visual.bWanted ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	Card->SetRenderOpacity(Opacity);

	FVector2D Size = Card->GetDesiredSize() * DPIScale;
	if (Size.X < 1.0 || Size.Y < 1.0)
	{
		Size = FVector2D(230.0f, 90.0f) * DPIScale;
	}

	// Card on the outer side of the node, vertically centred on it; slides out a little while revealing.
	const bool bLeft = Visual.ScreenEndpoint.X < ViewportSize.X * 0.5;
	const float Slide = (1.0f - Opacity) * 16.0f;
	const float Gap = (CardGap + Slide) * DPIScale;
	FVector2D TopLeft(bLeft ? Visual.ScreenEndpoint.X - Gap - Size.X : Visual.ScreenEndpoint.X + Gap, Visual.ScreenEndpoint.Y - Size.Y * 0.5);

	// Safe screen margins (handles any resolution / aspect ratio: recomputed every frame).
	const FVector2D Margin = ViewportSize * SafeMargin;
	TopLeft.X = FMath::Clamp(TopLeft.X, Margin.X, FMath::Max(Margin.X, ViewportSize.X - Margin.X - Size.X));
	TopLeft.Y = FMath::Clamp(TopLeft.Y, Margin.Y, FMath::Max(Margin.Y, ViewportSize.Y - Margin.Y - Size.Y));

	Card->SetAlignmentInViewport(FVector2D::ZeroVector);
	Card->SetPositionInViewport(TopLeft, true);
	Visual.bCardPlaced = true;
	Visual.CardTopLeft = TopLeft;
	Visual.CardPixels = Size;
}
