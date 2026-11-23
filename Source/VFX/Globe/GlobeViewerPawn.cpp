// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeViewerPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "Globe/GlobeDataLinksComponent.h"
#include "Globe/GlobeTimeOfDay.h"
#include "Globe/GlobeVoiceInputComponent.h"
#include "Globe/GlobeVoiceVisualizerWidget.h"
#include "Globe/GlobeVFXActor.h"
#include "Globe/GlobeVFXParameters.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeViewer, Log, All);

float FGlobeTransitionTiming::Evaluate(float Elapsed) const
{
	if (Elapsed <= Delay)
	{
		return 0.0f;
	}
	if (Duration <= UE_KINDA_SMALL_NUMBER || Elapsed >= Delay + Duration)
	{
		return 1.0f;
	}
	const double Alpha = (Elapsed - Delay) / Duration;
	return static_cast<float>(UKismetMathLibrary::Ease(0.0, 1.0, Alpha, Easing, BlendExp));
}

AGlobeViewerPawn::AGlobeViewerPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

#if WITH_EDITORONLY_DATA
	// World Partition: must exist when the GameMode starts the player, not stream in later
	// (otherwise a DefaultPawn is spawned first). Always loaded.
	bIsSpatiallyLoaded = false;
#endif

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bUsePawnControlRotation = false;

	// Microphone input for the bottom-centre voice visualizer.
	VoiceInput = CreateDefaultSubobject<UGlobeVoiceInputComponent>(TEXT("VoiceInput"));
	VoiceVisualizerClass = TSoftClassPtr<UGlobeVoiceVisualizerWidget>(FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s_C"),
		GlobeVFX::Assets::UIFolder, GlobeVFX::Assets::VoiceVisualizerWidgetName, GlobeVFX::Assets::VoiceVisualizerWidgetName)));

	// Both directions default to a straight crossfade: same start, same length, same curve.
	for (FGlobeTransitionTiming* Timing : { &HologramFadeOut, &RealGlobeFadeIn, &RealGlobeFadeOut, &HologramFadeIn })
	{
		Timing->Delay = 0.0f;
		Timing->Duration = 1.2f;
		Timing->Easing = EEasingFunc::EaseInOut;
	}

	// Not loaded here (constructor loads root assets at startup); resolved in BeginPlay.
	RealGlobeFadeMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/InterchangeAssets/gltf/MaterialInstances/MI_Unlit_Blend.MI_Unlit_Blend")));

	ZoomIn.Delay = 0.2f;
	ZoomIn.Duration = 1.5f;
	ZoomIn.Easing = EEasingFunc::EaseInOut;

	ZoomOut.Delay = 0.0f;
	ZoomOut.Duration = 1.2f;
	ZoomOut.Easing = EEasingFunc::EaseInOut;
}

FVector AGlobeViewerPawn::GetFocusLocation() const
{
	// The globe actor keeps its particle layer at the fitted sphere centre (the earth's pivot is offset).
	if (HologramGlobe)
	{
		const USceneComponent* Surface = HologramGlobe->GetSurfaceParticles();
		return Surface ? Surface->GetComponentLocation() : HologramGlobe->GetActorLocation();
	}
	return RealGlobe ? RealGlobe->GetActorLocation() : GetActorLocation();
}

void AGlobeViewerPawn::BeginPlay()
{
	Super::BeginPlay();

	// The start view is exactly the editor's camera preview: wherever the pawn and its Camera component were
	// placed. Mouse controls move the Camera component in world space; the pawn actor itself never moves.
	Root->SetMobility(EComponentMobility::Movable);
	Camera->SetMobility(EComponentMobility::Movable);
	PlacedTransform = Camera->GetComponentTransform();
	BaseFOV = Camera->FieldOfView;

	// Time of day: use the level's Globe Time Of Day, or add one so the [ / ] / T keys always work.
	if (!AGlobeTimeOfDay::Find(GetWorld()))
	{
		GetWorld()->SpawnActor<AGlobeTimeOfDay>();
	}

	HologramVisibility = { 1.0f, 1.0f, 1.0f };
	RealGlobeVisibility = { 0.0f, 0.0f, 0.0f };
	ZoomAmount = { 0.0f, 0.0f, 0.0f };

	if (HologramGlobe)
	{
		if (UStaticMeshComponent* HoloMesh = HologramGlobe->GetGlobeMesh())
		{
			HoloMesh->SetTranslucentSortPriority(HologramSortPriority);
		}
		ApplyHologram();
	}
	else
	{
		UE_LOG(LogGlobeViewer, Warning, TEXT("%s: HologramGlobe is not set."), *GetName());
	}

	if (RealGlobe)
	{
		// Rotating the globe moves the attached earth, which needs Movable mobility (Fab actors default to Static).
		USceneComponent* RealRoot = RealGlobe->GetRootComponent();
		if (RealRoot && RealRoot->Mobility != EComponentMobility::Movable)
		{
			UE_LOG(LogGlobeViewer, Log, TEXT("%s: switching %s to Movable so it can rotate with the globe (set Mobility to Movable on it to silence this)."),
				*GetName(), *RealGlobe->GetName());
			RealRoot->SetMobility(EComponentMobility::Movable);
		}
		SetupRealGlobeFadeMaterials();
		ApplyRealGlobe();
	}
	else
	{
		UE_LOG(LogGlobeViewer, Warning, TEXT("%s: RealGlobe is not set; S will only fade the hologram."), *GetName());
	}
}

void AGlobeViewerPawn::ToggleGlobe()
{
	bShowingRealGlobe = !bShowingRealGlobe;

	// Each track continues from where it is now, so pressing S mid-transition reverses smoothly.
	auto Retarget = [](FTrack& Track, float To)
	{
		Track.From = Track.Value;
		Track.To = To;
	};
	// With bKeepHologramOnSwap the hologram never fades; its centre clears instead (driven by the earth blend).
	Retarget(HologramVisibility, (bShowingRealGlobe && !bKeepHologramOnSwap) ? 0.0f : 1.0f);
	Retarget(RealGlobeVisibility, bShowingRealGlobe ? 1.0f : 0.0f);
	Retarget(ZoomAmount, (bShowingRealGlobe && bZoomOnSwap) ? 1.0f : 0.0f);

	TransitionStartTime = GetWorld()->GetTimeSeconds();
	bTransitionRunning = true;
	UpdateTransitions();

	// Earth mode: only the clicked (selected) item stays, as one larger detail card; back to all cards on return.
	if (HologramGlobe && HologramGlobe->GetDataLinks())
	{
		HologramGlobe->GetDataLinks()->SetDetailMode(bShowDetailCardOnSwap && bShowingRealGlobe);
	}
}

void AGlobeViewerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bVoiceDrivesGlobeRing && HologramGlobe && VoiceInput)
	{
		HologramGlobe->SetAudioEnergy(VoiceInput->GetLevel()); // the voice ring reacts to the microphone
	}

	if (bTransitionRunning)
	{
		UpdateTransitions();
	}
	if (bCameraControlled || bReturningToPlaced)
	{
		UpdateCamera(DeltaSeconds);
	}
}

void AGlobeViewerPawn::UpdateTransitions()
{
	const float Elapsed = static_cast<float>(GetWorld()->GetTimeSeconds() - TransitionStartTime);

	const FGlobeTransitionTiming& HoloTiming = bShowingRealGlobe ? HologramFadeOut : HologramFadeIn;
	const FGlobeTransitionTiming& RealTiming = bShowingRealGlobe ? RealGlobeFadeIn : RealGlobeFadeOut;
	const FGlobeTransitionTiming& ZoomTiming = bShowingRealGlobe ? ZoomIn : ZoomOut;

	auto Step = [Elapsed](FTrack& Track, const FGlobeTransitionTiming& Timing)
	{
		Track.Value = FMath::Lerp(Track.From, Track.To, Timing.Evaluate(Elapsed));
	};
	Step(HologramVisibility, HoloTiming);
	Step(RealGlobeVisibility, RealTiming);
	Step(ZoomAmount, ZoomTiming);

	ApplyHologram();
	ApplyRealGlobe();
	if (HologramGlobe)
	{
		// Particles/network stay alive in both modes; their look follows the same crossfade.
		HologramGlobe->SetEarthModeBlend(RealGlobeVisibility.Value);
	}
	Camera->SetFieldOfView(FMath::Lerp(BaseFOV, ZoomedFOV, ZoomAmount.Value));

	const float EndTime = FMath::Max3(HoloTiming.EndTime(), RealTiming.EndTime(), ZoomTiming.EndTime());
	if (Elapsed >= EndTime)
	{
		bTransitionRunning = false;
	}
}

void AGlobeViewerPawn::ApplyHologram()
{
	if (!HologramGlobe)
	{
		return;
	}
	HologramGlobe->SetFade(HologramVisibility.Value); // Material Fade parameter; hides the mesh at 0.
}

void AGlobeViewerPawn::SetupRealGlobeFadeMaterials()
{
	RealGlobeSlots.Reset();
	bRealGlobeUsingFadeMaterials = false;

	UMaterialInterface* FadeParent = RealGlobeFadeMaterial.LoadSynchronous();
	if (!FadeParent)
	{
		UE_LOG(LogGlobeViewer, Warning, TEXT("%s: RealGlobeFadeMaterial '%s' could not be loaded; the real globe will appear/disappear without fading."),
			*GetName(), *RealGlobeFadeMaterial.ToString());
		return;
	}

	TArray<UMeshComponent*> Meshes;
	RealGlobe->GetComponents(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		for (int32 SlotIndex = 0; SlotIndex < Mesh->GetNumMaterials(); ++SlotIndex)
		{
			FGlobeFadeMaterialSlot& Slot = RealGlobeSlots.AddDefaulted_GetRef();
			Slot.Mesh = Mesh;
			Slot.SlotIndex = SlotIndex;
			Slot.Original = Mesh->GetMaterial(SlotIndex);
			Slot.Fade = UMaterialInstanceDynamic::Create(FadeParent, this);

			// Carry the original's textures/colours over so the translucent copy looks identical.
			if (UMaterialInstance* OriginalInstance = Cast<UMaterialInstance>(Slot.Original))
			{
				Slot.Fade->CopyParameterOverrides(OriginalInstance);
			}
			if (Slot.Original)
			{
				Slot.Original->GetVectorParameterValue(FHashedMaterialParameterInfo(RealGlobeOpacityParameter), Slot.BaseFactor);
			}
		}
	}
}

void AGlobeViewerPawn::SetRealGlobeFadeMaterialsActive(bool bActive)
{
	for (const FGlobeFadeMaterialSlot& Slot : RealGlobeSlots)
	{
		if (Slot.Mesh)
		{
			Slot.Mesh->SetMaterial(Slot.SlotIndex, bActive ? static_cast<UMaterialInterface*>(Slot.Fade) : Slot.Original.Get());
		}
	}
	// Switching translucent -> opaque left the earth drawn with a stale render state (invisible, no depth)
	// until something else refreshed it. Rebuild it explicitly after every material switch.
	for (const FGlobeFadeMaterialSlot& Slot : RealGlobeSlots)
	{
		if (Slot.Mesh)
		{
			Slot.Mesh->MarkRenderStateDirty();
		}
	}
	bRealGlobeUsingFadeMaterials = bActive;
}

void AGlobeViewerPawn::ApplyRealGlobe()
{
	if (!RealGlobe)
	{
		return;
	}
	const float Visibility = RealGlobeVisibility.Value;
	RealGlobe->SetActorHiddenInGame(Visibility <= 0.0f);

	// Translucent copies while partially visible or when Earth Opacity < 1; fully solid uses the original
	// opaque materials.
	const float Alpha = Visibility * FMath::Clamp(RealGlobeOpacity, 0.0f, 1.0f);
	const bool bNeedFadeMaterials = Visibility > 0.0f && Alpha < 1.0f && RealGlobeSlots.Num() > 0;
	if (bNeedFadeMaterials != bRealGlobeUsingFadeMaterials)
	{
		SetRealGlobeFadeMaterialsActive(bNeedFadeMaterials);
	}
	if (bNeedFadeMaterials)
	{
		for (const FGlobeFadeMaterialSlot& Slot : RealGlobeSlots)
		{
			const FLinearColor& Base = Slot.BaseFactor;
			Slot.Fade->SetVectorParameterValue(RealGlobeOpacityParameter, FLinearColor(Base.R, Base.G, Base.B, Base.A * Alpha));
		}
	}
}

void AGlobeViewerPawn::BeginCameraControl()
{
	if (bCameraControlled)
	{
		return;
	}
	// Derive orbit parameters from the camera's current position, so taking control causes no jump
	// in position (the view turns smoothly to face the globe).
	const FVector Offset = Camera->GetComponentLocation() - (GetFocusLocation() + PanOffset);
	const FRotator OffsetRot = Offset.Rotation();
	OrbitYaw = OffsetRot.Yaw;
	OrbitPitch = FMath::Clamp(OffsetRot.Pitch, MinPitch, MaxPitch);
	OrbitDistance = FMath::Clamp(Offset.Size(), MinZoomDistance, FMath::Max(MinZoomDistance, MaxZoomDistance));
	const FQuat LookAtGlobe = (-Offset).Rotation().Quaternion();
	FramingOffset = LookAtGlobe.Inverse() * Camera->GetComponentQuat();
	bCameraControlled = true;
	bReturningToPlaced = false;
}

void AGlobeViewerPawn::UpdateCamera(float DeltaSeconds)
{
	FVector TargetLocation = PlacedTransform.GetLocation();
	FRotator TargetRotation = PlacedTransform.Rotator();

	if (bCameraControlled)
	{
		const FVector Focus = GetFocusLocation() + PanOffset;
		TargetLocation = Focus + FRotator(OrbitPitch, OrbitYaw, 0.0f).Vector() * OrbitDistance;
		TargetRotation = ((Focus - TargetLocation).Rotation().Quaternion() * FramingOffset).Rotator();
	}

	const FVector CurrentLocation = Camera->GetComponentLocation();
	const FRotator CurrentRotation = Camera->GetComponentRotation();
	if (CurrentLocation.Equals(TargetLocation, 0.05) && CurrentRotation.Equals(TargetRotation, 0.01))
	{
		if (bReturningToPlaced)
		{
			Camera->SetWorldLocationAndRotation(TargetLocation, TargetRotation); // Land exactly on the placed view.
			bReturningToPlaced = false;
		}
		return;
	}
	Camera->SetWorldLocationAndRotation(
		FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaSeconds, CameraSmoothing),
		FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaSeconds, CameraSmoothing));
}

void AGlobeViewerPawn::BuildInputObjects()
{
	if (InputContext)
	{
		return;
	}

	SwapAction = NewObject<UInputAction>(this, TEXT("IA_GlobeSwap"));
	SwapAction->ValueType = EInputActionValueType::Boolean;
	TogglePrismsAction = NewObject<UInputAction>(this, TEXT("IA_GlobeTogglePrisms"));
	TogglePrismsAction->ValueType = EInputActionValueType::Boolean;

	LookAction = NewObject<UInputAction>(this, TEXT("IA_GlobeLook"));
	LookAction->ValueType = EInputActionValueType::Axis2D;

	auto MakeButton = [this](const TCHAR* Name)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Boolean;
		return Action;
	};
	OrbitHoldAction = MakeButton(TEXT("IA_GlobeOrbitHold"));
	RotateHoldAction = MakeButton(TEXT("IA_GlobeRotateHold"));
	PanHoldAction = MakeButton(TEXT("IA_GlobePanHold"));
	ResetViewAction = MakeButton(TEXT("IA_GlobeResetView"));
	TimeBackAction = MakeButton(TEXT("IA_GlobeTimeBack"));
	TimeForwardAction = MakeButton(TEXT("IA_GlobeTimeForward"));
	TimeAutoAction = MakeButton(TEXT("IA_GlobeTimeAuto"));
	ZoomInAction = MakeButton(TEXT("IA_GlobeZoomIn"));
	ZoomOutAction = MakeButton(TEXT("IA_GlobeZoomOut"));

	InputContext = NewObject<UInputMappingContext>(this, TEXT("IMC_GlobeViewer"));
	InputContext->MapKey(SwapAction, SwapKey);
	InputContext->MapKey(TogglePrismsAction, TogglePrismsKey);
	InputContext->MapKey(TimeBackAction, TimeBackKey);
	InputContext->MapKey(TimeForwardAction, TimeForwardKey);
	InputContext->MapKey(TimeAutoAction, TimeAutoKey);
	InputContext->MapKey(LookAction, EKeys::Mouse2D);
	// Wheel as discrete scroll-up/down key events (one per notch), plus keyboard keys.
	InputContext->MapKey(ZoomInAction, EKeys::MouseScrollUp);
	InputContext->MapKey(ZoomOutAction, EKeys::MouseScrollDown);
	InputContext->MapKey(ZoomInAction, ZoomInKey);
	InputContext->MapKey(ZoomOutAction, ZoomOutKey);
	InputContext->MapKey(OrbitHoldAction, OrbitButton);
	InputContext->MapKey(RotateHoldAction, RotateGlobeButton);
	InputContext->MapKey(PanHoldAction, PanButton);
	InputContext->MapKey(ResetViewAction, ResetViewKey);
}

void AGlobeViewerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogGlobeViewer, Error, TEXT("%s: needs EnhancedInputComponent (Project Settings > Input > Default Input Component Class)."), *GetName());
		return;
	}

	BuildInputObjects();
	EnhancedInput->BindAction(SwapAction, ETriggerEvent::Started, this, &AGlobeViewerPawn::OnSwapPressed);
	EnhancedInput->BindAction(TogglePrismsAction, ETriggerEvent::Started, this, &AGlobeViewerPawn::OnTogglePrismsPressed);
	// Time of day: [ / ] fire every frame while held; T toggles auto-advance.
	EnhancedInput->BindActionValueLambda(TimeBackAction, ETriggerEvent::Triggered, [this](const FInputActionValue&) { ScrubTime(-1.0f); });
	EnhancedInput->BindActionValueLambda(TimeForwardAction, ETriggerEvent::Triggered, [this](const FInputActionValue&) { ScrubTime(1.0f); });
	EnhancedInput->BindActionValueLambda(TimeAutoAction, ETriggerEvent::Started, [this](const FInputActionValue&)
	{
		if (AGlobeTimeOfDay* TimeOfDay = AGlobeTimeOfDay::Find(GetWorld()))
		{
			TimeOfDay->SetAutoAdvance(!TimeOfDay->IsAutoAdvancing());
		}
	});
	EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AGlobeViewerPawn::OnLook);
	EnhancedInput->BindActionValueLambda(ZoomInAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ZoomBy(1.0f); });
	EnhancedInput->BindActionValueLambda(ZoomOutAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ZoomBy(-1.0f); });
	EnhancedInput->BindAction(ResetViewAction, ETriggerEvent::Started, this, &AGlobeViewerPawn::OnResetView);

	// Held-button state for the drag modes.
	auto BindHold = [this, EnhancedInput](UInputAction* Action, bool AGlobeViewerPawn::* Flag)
	{
		EnhancedInput->BindActionValueLambda(Action, ETriggerEvent::Started, [this, Flag](const FInputActionValue&) { this->*Flag = true; });
		EnhancedInput->BindActionValueLambda(Action, ETriggerEvent::Completed, [this, Flag](const FInputActionValue&) { this->*Flag = false; });
	};
	BindHold(OrbitHoldAction, &AGlobeViewerPawn::bOrbitHeld);
	BindHold(RotateHoldAction, &AGlobeViewerPawn::bRotateHeld);
	BindHold(PanHoldAction, &AGlobeViewerPawn::bPanHeld);
}

void AGlobeViewerPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Visible cursor for clicking data cards. Game-and-UI input: clicks on a card go to the card, clicks and
	// drags elsewhere still reach the camera controls.
	if (APlayerController* PC = Cast<APlayerController>(NewController); PC && bShowCursorForCards)
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}

	ShowVoiceVisualizer();
	// If this pawn streamed in after the GameMode already spawned a DefaultPawn for the player, that
	// spare pawn (the floating sphere) is left behind unpossessed. Remove it. Only runtime-spawned ones:
	// DefaultPawns placed in the level on purpose are left alone.
	for (TActorIterator<ADefaultPawn> It(GetWorld()); It; ++It)
	{
		if (!It->GetController() && !It->HasAnyFlags(RF_WasLoaded))
		{
			It->Destroy();
		}
	}
}

#if WITH_EDITOR
void AGlobeViewerPawn::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		ApplyRealGlobe(); // Live-tune Earth Opacity during Play (press K to keep it after stopping).
		ApplyVoiceVisualizerSettings(); // and the voice visualizer's look
	}
}
#endif

void AGlobeViewerPawn::PawnClientRestart()
{
	Super::PawnClientRestart(); // Calls SetupPlayerInputComponent, which builds InputContext.

	const APlayerController* PC = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		if (InputContext)
		{
			Subsystem->AddMappingContext(InputContext, 0);
		}
	}
}

void AGlobeViewerPawn::UnPossessed()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		if (InputContext)
		{
			Subsystem->RemoveMappingContext(InputContext);
		}
	}

	Super::UnPossessed();
}

void AGlobeViewerPawn::OnSwapPressed(const FInputActionValue& Value)
{
	ToggleGlobe();
}

void AGlobeViewerPawn::OnTogglePrismsPressed(const FInputActionValue& Value)
{
	TogglePrisms();
}

void AGlobeViewerPawn::ScrubTime(float Direction)
{
	if (AGlobeTimeOfDay* TimeOfDay = AGlobeTimeOfDay::Find(GetWorld()))
	{
		TimeOfDay->AddHours(Direction * TimeOfDay->GetScrubSpeed() * GetWorld()->GetDeltaSeconds());
	}
}

void AGlobeViewerPawn::TogglePrisms()
{
	if (HologramGlobe)
	{
		HologramGlobe->TogglePrisms();
	}
}

void AGlobeViewerPawn::OnLook(const FInputActionValue& Value)
{
	if (!bEnableMouseCamera)
	{
		return;
	}
	const FVector2D Delta = Value.Get<FVector2D>();
	if (Delta.IsNearlyZero())
	{
		return;
	}

	if (bRotateHeld && HologramGlobe && !HologramGlobe->IsGlobeRotationLocked())
	{
		// Rotate the globe rig about the view axes so the surface under the cursor follows the drag.
		const FVector CamRight = Camera->GetRightVector();
		const FVector CamUp = Camera->GetUpVector();
		const FQuat Spin(CamUp, FMath::DegreesToRadians(-Delta.X * GlobeRotateSensitivity));
		const FQuat Tilt(CamRight, FMath::DegreesToRadians(Delta.Y * GlobeRotateSensitivity));
		HologramGlobe->AddActorWorldRotation(Spin * Tilt);
		return;
	}

	if (bOrbitHeld)
	{
		BeginCameraControl();
		// Grab-style: drag right moves the view right; drag up moves the camera down.
		OrbitYaw += Delta.X * OrbitSensitivity;
		OrbitPitch = FMath::Clamp(OrbitPitch - Delta.Y * OrbitSensitivity, MinPitch, MaxPitch);
	}
	else if (bPanHeld && bAllowPan)
	{
		BeginCameraControl();
		const float Scale = PanSensitivity * OrbitDistance * 0.002f;
		PanOffset -= (Camera->GetRightVector() * Delta.X + Camera->GetUpVector() * Delta.Y) * Scale;
		PanOffset = PanOffset.GetClampedToMaxSize(MaxPanDistance);
	}
}

void AGlobeViewerPawn::ZoomBy(float Notches)
{
	if (!bEnableMouseCamera)
	{
		return;
	}
	BeginCameraControl();
	OrbitDistance = FMath::Clamp(OrbitDistance * (1.0f - Notches * WheelZoomStep), MinZoomDistance, FMath::Max(MinZoomDistance, MaxZoomDistance));
	UE_LOG(LogGlobeViewer, Verbose, TEXT("Zoom %+.0f -> %.0f cm from globe centre"), Notches, OrbitDistance);
}

void AGlobeViewerPawn::OnResetView(const FInputActionValue& Value)
{
	bCameraControlled = false;
	bReturningToPlaced = true;
	PanOffset = FVector::ZeroVector;
	if (bResetViewResetsGlobeRotation && HologramGlobe)
	{
		HologramGlobe->ResetGlobeRotation();
	}
}

void AGlobeViewerPawn::ShowVoiceVisualizer()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!bShowVoiceVisualizer || !PC || !PC->IsLocalController() || VoiceWidget)
	{
		return;
	}
	// The Widget Blueprint (editable in the editor); the plain C++ class builds the same default layout.
	TSubclassOf<UGlobeVoiceVisualizerWidget> WidgetClass = VoiceVisualizerClass.LoadSynchronous();
	UE_CLOG(!WidgetClass && !VoiceVisualizerClass.IsNull(), LogGlobeViewer, Warning,
		TEXT("Voice visualizer: %s not found, using the built-in layout."), *VoiceVisualizerClass.ToString());
	VoiceWidget = CreateWidget<UGlobeVoiceVisualizerWidget>(PC, WidgetClass ? WidgetClass.Get() : UGlobeVoiceVisualizerWidget::StaticClass());
	if (!VoiceWidget)
	{
		return;
	}
	VoiceWidget->SetVoiceInput(VoiceInput);
	VoiceWidget->SetVisibility(ESlateVisibility::HitTestInvisible); // never blocks clicks on the cards or the globe
	VoiceWidget->AddToViewport(5); // full-screen layer; the layout inside places the visualizer
}

void AGlobeViewerPawn::ApplyVoiceVisualizerSettings()
{
	if (!VoiceWidget)
	{
		ShowVoiceVisualizer(); // e.g. switched on while playing
		return;
	}
	VoiceWidget->SetVisibility(bShowVoiceVisualizer ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
