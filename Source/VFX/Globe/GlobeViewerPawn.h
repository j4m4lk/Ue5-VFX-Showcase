// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetMathLibrary.h"
#include "GlobeViewerPawn.generated.h"

class AGlobeVFXActor;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UGlobeVoiceInputComponent;
class UGlobeVoiceVisualizerWidget;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
struct FInputActionValue;

/** A material slot on the real globe: its original material and the translucent copy used while fading. */
USTRUCT()
struct FGlobeFadeMaterialSlot
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMeshComponent> Mesh;

	UPROPERTY()
	int32 SlotIndex = 0;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Original;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Fade;

	/** Opacity-parameter colour copied from the original, so RGB tint is preserved while alpha fades. */
	FLinearColor BaseFactor = FLinearColor::White;
};

/** Timing and easing for one part of the swap transition. */
USTRUCT(BlueprintType)
struct FGlobeTransitionTiming
{
	GENERATED_BODY()

	/** Seconds after S is pressed before this part starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "0.0", Units = "s"))
	float Delay = 0.0f;

	/** Seconds this part takes once started. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "0.0", Units = "s"))
	float Duration = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	TEnumAsByte<EEasingFunc::Type> Easing = EEasingFunc::EaseInOut;

	/** Strength of the EaseIn / EaseOut / EaseInOut curves (2 = quadratic, 3 = cubic, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "1.0", UIMax = "8.0"))
	float BlendExp = 2.0f;

	/** Eased progress 0..1 for the given seconds since the transition began. */
	float Evaluate(float Elapsed) const;

	float EndTime() const { return Delay + Duration; }
};

/**
 * Showcase camera + globe swap. Place one in the level where you want the camera and rotate it to
 * frame the shot (or right-click > Pilot it). It auto-possesses Player 0.
 *  - The camera starts at the placed transform and stays there until the mouse is used:
 *    wheel = zoom (MinZoomDistance..MaxZoomDistance), LMB drag = orbit, RMB drag = rotate the globe,
 *    MMB drag = pan (if Allow Pan), F = back to the placed view (and the globe's start rotation), hold [ / ] = time of day back / forward, T = let the day run.
 *  - SwapKey (S) crossfades the hologram out and RealGlobe in; pressing it again swaps back.
 *  - TogglePrismsKey (R) switches the prisms on (they grow in) and off (they shrink and fade).
 *  - Only opacity changes; both globes keep the transform you placed them with.
 * Input is built in code with Enhanced Input, so no input assets are needed.
 */
UCLASS(ClassGroup = (AIGlobe), meta = (DisplayName = "Globe Viewer Pawn"))
class VFX_API AGlobeViewerPawn : public APawn
{
	GENERATED_BODY()

public:
	AGlobeViewerPawn();

	/** Swaps between the hologram and the real globe. Safe to call mid-transition (it reverses smoothly). */
	UFUNCTION(BlueprintCallable, Category = "Globe Viewer")
	void ToggleGlobe();

	UFUNCTION(BlueprintPure, Category = "Globe Viewer")
	bool IsShowingRealGlobe() const { return bShowingRealGlobe; }

	/** The on-screen voice visualizer (null until play starts). */
	UFUNCTION(BlueprintPure, Category = "Globe Viewer")
	UGlobeVoiceVisualizerWidget* GetVoiceVisualizerWidget() const { return VoiceWidget; }

	/** Switches HologramGlobe's prisms on or off (TogglePrismsKey). */
	UFUNCTION(BlueprintCallable, Category = "Globe Viewer")
	void TogglePrisms();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;
#if WITH_EDITOR
	/** Applies Earth Opacity and Voice Visualizer edits made while playing. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe Viewer")
	TObjectPtr<USceneComponent> Root;

	/** Field of view is edited here (Camera Settings > Field Of View). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe Viewer")
	TObjectPtr<UCameraComponent> Camera;

	// ---------------- Globes ----------------

	/** Shown on Play; faded out on swap. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Globe Viewer|Globes")
	TObjectPtr<AGlobeVFXActor> HologramGlobe;

	/** Hidden on Play; revealed on swap (e.g. the Fab earth StaticMeshActor). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Globe Viewer|Globes")
	TObjectPtr<AActor> RealGlobe;

	/** On: S only fades the earth model in/out. The hologram stays visible and its centre turns clear so the
	 *  earth texture reads through (see GlobeVFXActor > Hologram Center Clear), while its rim keeps glowing.
	 *  Off: the hologram fades out and the earth replaces it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Globes")
	bool bKeepHologramOnSwap = true;

	/** Opacity the earth fades up to when swapped in. 1 = fully solid (its original opaque materials).
	 *  Below 1 it stays slightly see-through, so points under or behind its surface show through. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Globes", meta = (DisplayName = "Earth Opacity", ClampMin = "0.0", ClampMax = "1.0"))
	float RealGlobeOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Input")
	FKey SwapKey = EKeys::S;

	/** Switches the prisms on and off (they grow from their min to max size, and back). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Input")
	FKey TogglePrismsKey = EKeys::R;

	/** Hold to move the time of day back / forward (Globe Time Of Day; one is spawned at play start if the level has
	 *  none). Press TimeAutoKey to let the day run by itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Input")
	FKey TimeBackKey = EKeys::LeftBracket;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Input")
	FKey TimeForwardKey = EKeys::RightBracket;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Input")
	FKey TimeAutoKey = EKeys::T;

	// ---------------- Voice visualizer (bottom centre) ----------------

	/** Microphone input: source (Microphone / Simulated Speech / Off), detection threshold, sensitivity, hold time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Voice Visualizer")
	TObjectPtr<UGlobeVoiceInputComponent> VoiceInput;

	/** Add the voice visualizer to the screen when play starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Voice Visualizer")
	bool bShowVoiceVisualizer = true;

	/**
	 * The on-screen widget. Its look (texts, alpha, colours, bar and globe sizes) and position are edited in this
	 * Widget Blueprint: open it, select "Visualizer" in the Hierarchy and use the Details panel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Voice Visualizer")
	TSoftClassPtr<UGlobeVoiceVisualizerWidget> VoiceVisualizerClass;

	/** Also drive the globe's cloud ring (voice ring) with the microphone level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Voice Visualizer")
	bool bVoiceDrivesGlobeRing = false;

	/** Shows / hides the visualizer to match Show Voice Visualizer (called after edits while playing). */
	UFUNCTION(BlueprintCallable, Category = "Globe Viewer")
	void ApplyVoiceVisualizerSettings();

	// ---------------- Crossfade: S pressed (hologram -> real globe) ----------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap To Real Globe")
	FGlobeTransitionTiming HologramFadeOut;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap To Real Globe")
	FGlobeTransitionTiming RealGlobeFadeIn;

	// ---------------- Crossfade: S pressed again (real globe -> hologram) ----------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap To Hologram")
	FGlobeTransitionTiming RealGlobeFadeOut;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap To Hologram")
	FGlobeTransitionTiming HologramFadeIn;

	// ---------------- Real globe fade setup ----------------

	/**
	 * Translucent material used on the real globe only while it fades. The globe's own texture/colour
	 * parameters are copied onto it, and its original (opaque) materials are restored once fully visible.
	 * The default suits glTF/Fab imports (Interchange M_Unlit family). Use MI_Unlit_Blend_DS if the mesh
	 * needs two-sided rendering; for lit imports use /InterchangeAssets/gltf/MaterialInstances/MI_Default_Blend.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Real Globe Fade Setup")
	TSoftObjectPtr<UMaterialInterface> RealGlobeFadeMaterial;

	/** Vector parameter on RealGlobeFadeMaterial whose alpha is opacity (glTF: BaseColorFactor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Real Globe Fade Setup")
	FName RealGlobeOpacityParameter = TEXT("BaseColorFactor");

	/** Draw order while both globes are translucent. Higher draws on top: the hologram shell over the earth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Real Globe Fade Setup")
	int32 HologramSortPriority = 1;

	// ---------------- Optional camera zoom on swap ----------------

	/** Narrow the camera FOV when showing the real globe. The camera itself never moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap Zoom")
	bool bZoomOnSwap = false;

	/** FOV while the real globe is shown. Lower than the camera's FOV = zoomed in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap Zoom", meta = (EditCondition = "bZoomOnSwap", ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float ZoomedFOV = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap Zoom", meta = (EditCondition = "bZoomOnSwap"))
	FGlobeTransitionTiming ZoomIn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Swap Zoom", meta = (EditCondition = "bZoomOnSwap"))
	FGlobeTransitionTiming ZoomOut;

	// ---------------- Mouse camera ----------------

	/** Mouse camera controls in both modes. The camera stays exactly at its placed transform until you use them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera")
	bool bEnableMouseCamera = true;

	/** Shows the mouse cursor so data cards can be clicked (the drag controls below keep working). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (DisplayName = "Show Cursor (Click Cards)"))
	bool bShowCursorForCards = true;

	/** On: while the earth is shown (S), the data display switches to one detailed card for the clicked item. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Globes")
	bool bShowDetailCardOnSwap = true;

	/** Hold and drag to orbit the camera around the globe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	FKey OrbitButton = EKeys::LeftMouseButton;

	/** Hold and drag to rotate the globe itself (hologram, attached earth, particles and network together). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	FKey RotateGlobeButton = EKeys::RightMouseButton;

	/** Hold and drag to pan the camera's focus point across the view. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	FKey PanButton = EKeys::MiddleMouseButton;

	/** Keyboard zoom, in addition to the mouse wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	FKey ZoomInKey = EKeys::Equals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	FKey ZoomOutKey = EKeys::Hyphen;

	/** Returns the camera to its placed transform (the globe keeps any rotation you gave it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	FKey ResetViewKey = EKeys::F;

	/** Closest the camera may get to the globe centre (max zoom in), cm. Keep it above the globe radius (~1100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera", ClampMin = "10.0", Units = "cm"))
	float MinZoomDistance = 1500.0f;

	/** Furthest the camera may get from the globe centre (max zoom out), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera", ClampMin = "10.0", Units = "cm"))
	float MaxZoomDistance = 12000.0f;

	/** Fraction of the current distance moved per mouse-wheel notch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera", ClampMin = "0.01", ClampMax = "0.5"))
	float WheelZoomStep = 0.1f;

	/** Degrees per unit of mouse movement while orbiting. Negative inverts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	float OrbitSensitivity = 0.2f;

	/** Degrees per unit of mouse movement while rotating the globe. Negative inverts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	float GlobeRotateSensitivity = 0.25f;

	/** Pan speed; scaled by the current distance so it feels the same zoomed in or out. Negative inverts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	float PanSensitivity = 1.0f;

	/** How far the focus point may be panned away from the globe centre, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera", ClampMin = "0.0", Units = "cm"))
	float MaxPanDistance = 3000.0f;

	/** Off (default): the globe always stays at the centre of the view; middle-drag panning is disabled and the camera
	 *  only orbits (left-drag) and zooms (wheel) around it. On: middle-drag pans the focus point away from it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera"))
	bool bAllowPan = false;

	/** The reset key (F) also turns the globe back to the orientation it had when play started. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera")
	bool bResetViewResetsGlobeRotation = true;

	/** Orbit elevation limits (degrees above/below the globe's equator plane). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera", ClampMin = "-89.0", ClampMax = "89.0"))
	float MinPitch = -80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera", ClampMin = "-89.0", ClampMax = "89.0"))
	float MaxPitch = 80.0f;

	/** Camera smoothing: higher follows the mouse more tightly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Globe Viewer|Mouse Camera", meta = (EditCondition = "bEnableMouseCamera", ClampMin = "0.5"))
	float CameraSmoothing = 8.0f;

private:
	/** A value animated from its value at the last toggle toward 0 or 1. */
	struct FTrack
	{
		float Value = 0.0f;
		float From = 0.0f;
		float To = 0.0f;
	};

	void BuildInputObjects();
	void OnSwapPressed(const FInputActionValue& Value);
	void OnTogglePrismsPressed(const FInputActionValue& Value);
	void ScrubTime(float Direction);
	void OnLook(const FInputActionValue& Value);
	/** Positive = zoom in. One mouse-wheel notch or key press = 1. */
	void ZoomBy(float Notches);
	void OnResetView(const FInputActionValue& Value);

	/** Switches from the placed camera to mouse control, starting from wherever the camera is now. */
	void BeginCameraControl();

	void UpdateTransitions();
	void UpdateCamera(float DeltaSeconds);
	void ApplyHologram();
	void ApplyRealGlobe();
	void SetupRealGlobeFadeMaterials();
	void SetRealGlobeFadeMaterialsActive(bool bActive);
	FVector GetFocusLocation() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SwapAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TogglePrismsAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TimeBackAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TimeForwardAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TimeAutoAction;

	UPROPERTY(Transient)
	TObjectPtr<UGlobeVoiceVisualizerWidget> VoiceWidget;

	void ShowVoiceVisualizer();

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ZoomInAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ZoomOutAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> OrbitHoldAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RotateHoldAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PanHoldAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ResetViewAction;

	bool bOrbitHeld = false;
	bool bRotateHeld = false;
	bool bPanHeld = false;

	bool bShowingRealGlobe = false;
	bool bTransitionRunning = false;
	double TransitionStartTime = 0.0;

	/** 1 = hologram fully visible. */
	FTrack HologramVisibility;
	/** 1 = real globe fully visible. */
	FTrack RealGlobeVisibility;
	/** 1 = fully zoomed (ZoomedFOV). */
	FTrack ZoomAmount;

	/** One entry per material slot on RealGlobe's mesh components. */
	UPROPERTY(Transient)
	TArray<FGlobeFadeMaterialSlot> RealGlobeSlots;

	/** True while RealGlobeSlots' translucent fade materials are applied instead of the originals. */
	bool bRealGlobeUsingFadeMaterials = false;
	float BaseFOV = 90.0f;

	/** Transform the camera was placed at; Reset View returns here. */
	FTransform PlacedTransform;

	/** True once the mouse has taken over; false = camera sits at (or eases back to) PlacedTransform. */
	bool bCameraControlled = false;
	bool bReturningToPlaced = false;
	float OrbitYaw = 0.0f;
	float OrbitPitch = 0.0f;
	float OrbitDistance = 0.0f;
	FVector PanOffset = FVector::ZeroVector;
	/** Placed camera rotation relative to "look straight at the globe", kept while orbiting/zooming so the
	 *  shot's composition (e.g. globe off-centre) is preserved. */
	FQuat FramingOffset = FQuat::Identity;
};
