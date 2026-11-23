// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeVFXActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Globe/GlobeCloudRingComponent.h"
#include "Globe/GlobeDataLinksComponent.h"
#include "Globe/GlobeNetworkComponent.h"
#include "Globe/GlobePrismRibbonComponent.h"
#include "Globe/GlobeVFXParameters.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Scalability.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeVFX, Log, All);

AGlobeVFXActor::AGlobeVFXActor()
{
	// Surface animation runs in the material; Tick is enabled only while FadeTo is running.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

#if WITH_EDITORONLY_DATA
	// World Partition: keep the globe loaded from the start so the viewer pawn can find it at BeginPlay.
	bIsSpatiallyLoaded = false;
#endif

	GlobeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GlobeMesh"));
	SetRootComponent(GlobeMesh);
	GlobeMesh->SetMobility(EComponentMobility::Movable); // Swap transitions scale the actor at runtime.
	GlobeMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	GlobeMesh->SetCastShadow(false);
	GlobeMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		GlobeMesh->SetStaticMesh(SphereMesh.Object);
	}

	// No default material is loaded here: loading assets in constructors roots them at startup and
	// hides the reference from the cooker. See OnConstruction for the editor-time default.

	// Phase 2 layers. Attached to the root so they follow location and rotation; absolute scale because
	// their size comes from the fitted radius (re-fitted when the root scale changes).
	SurfaceParticles = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SurfaceParticles"));
	SurfaceParticles->SetupAttachment(GlobeMesh);
	SurfaceParticles->SetUsingAbsoluteScale(true);
	SurfaceParticles->SetCastShadow(false);
	SurfaceParticles->bAutoActivate = true;

	Network = CreateDefaultSubobject<UGlobeNetworkComponent>(TEXT("Network"));
	Network->SetupAttachment(GlobeMesh);
	Network->SetUsingAbsoluteScale(true);

	// Phase 3 cloud ring: same attachment and fitted-sphere placement as the other layers, so it follows the
	// globe's location, rotation and scale and keeps running through the S crossfade.
	CloudRingCore = CreateDefaultSubobject<UGlobeCloudRingComponent>(TEXT("CloudRingCore"));
	CloudRingCore->SetupAttachment(GlobeMesh);
	CloudRingCore->SetUsingAbsoluteScale(true);

	CloudRingMist = CreateDefaultSubobject<UNiagaraComponent>(TEXT("CloudRingMist"));
	CloudRingMist->SetupAttachment(GlobeMesh);
	CloudRingMist->SetUsingAbsoluteScale(true);
	CloudRingMist->SetCastShadow(false);
	CloudRingMist->bAutoActivate = true;

	// Prisms (ribbon mesh + sparkles): same attachment and absolute-scale rule as the other layers (radius comes from the measured,
	// already-scaled globe), so they follow the globe's location and scale and keep running through the S crossfade.
	PrismRibbon = CreateDefaultSubobject<UGlobePrismRibbonComponent>(TEXT("PrismRibbon"));
	PrismRibbon->SetupAttachment(GlobeMesh);
	PrismRibbon->SetUsingAbsoluteScale(true);

	Prisms = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Prisms"));
	Prisms->SetupAttachment(GlobeMesh);
	Prisms->SetUsingAbsoluteScale(true);
	Prisms->SetCastShadow(false);
	Prisms->bAutoActivate = true;

	// Night-side city lights: a sphere shell just above the earth (placed and sized in RefreshCityLights).
	CityLights = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CityLights"));
	CityLights->SetupAttachment(GlobeMesh);
	CityLights->SetUsingAbsoluteScale(true);
	CityLights->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	CityLights->SetCastShadow(false);
	CityLights->SetGenerateOverlapEvents(false);
	if (SphereMesh.Succeeded())
	{
		CityLights->SetStaticMesh(SphereMesh.Object);
	}

	// Four-link data display (camera-relative links + UMG cards; empty until items are supplied).
	DataLinks = CreateDefaultSubobject<UGlobeDataLinksComponent>(TEXT("DataLinks"));
}

void AGlobeVFXActor::FixDataItemIds()
{
	TSet<FName> Used;
	for (FGlobeDataItem& Item : DataItems)
	{
		if (Item.Id.IsNone() || Used.Contains(Item.Id))
		{
			FString Base;
			for (const TCHAR C : Item.Title.ToString())
			{
				if (FChar::IsAlnum(C) || C == TEXT('_'))
				{
					Base.AppendChar(C);
				}
			}
			if (Base.IsEmpty())
			{
				Base = TEXT("Item");
			}
			FName Candidate(*Base);
			for (int32 Suffix = 2; Used.Contains(Candidate); ++Suffix)
			{
				Candidate = FName(*FString::Printf(TEXT("%s_%d"), *Base, Suffix));
			}
			Item.Id = Candidate;
		}
		Used.Add(Item.Id);
	}
}

void AGlobeVFXActor::AddDataItemFromName()
{
	if (DataItems.Num() >= UGlobeDataLinksComponent::MaxItems)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: already %d data items (the maximum shown); not adding another."), *GetName(), DataItems.Num());
		return;
	}
	Modify();
	FGlobeDataItem Item;
	Item.Title = NewDataItemName.IsEmptyOrWhitespace() ? FText::FromString(FString::Printf(TEXT("Item %d"), DataItems.Num() + 1)) : NewDataItemName;
	Item.Value = FText::FromString(TEXT("-")); // placeholder until you (or your data source) set the value
	DataItems.Add(Item);
	FixDataItemIds();
	NewDataItemName = FText::GetEmpty();
	ApplyDataItems();
}

void AGlobeVFXActor::RemoveLastDataItem()
{
	if (DataItems.Num() > 0)
	{
		Modify();
		DataItems.Pop();
		ApplyDataItems();
	}
}

void AGlobeVFXActor::ClearAllDataItems()
{
	Modify();
	DataItems.Reset();
	ApplyDataItems();
}

void AGlobeVFXActor::ApplyDataItems()
{
	FixDataItemIds();
	const UWorld* World = GetWorld();
	if (DataLinks && World && World->IsGameWorld())
	{
		// Same Ids keep their streams; only changed text updates. More than 4: the display shows the first 4.
		DataLinks->SetDataItems(DataItems);
	}
}

FVector AGlobeVFXActor::GetGlobeCenter() const
{
	return SurfaceParticles ? SurfaceParticles->GetComponentLocation() : GetActorLocation();
}

float AGlobeVFXActor::GetGlobeSurfaceRadius() const
{
	return RingGlobeRadius > 0.0f ? RingGlobeRadius : SurfaceRadius;
}

void AGlobeVFXActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	// Editor convenience: a freshly placed globe picks up the generated instance. The value is then
	// saved on the level actor like a manual assignment, so cooking sees the reference.
	const UWorld* World = GetWorld();
	if (!GlobeMaterial && World && !World->IsGameWorld() && !IsTemplate())
	{
		static const FSoftObjectPath DefaultMaterialPath(TEXT("/Game/AIGlobe/Materials/MI_GlobeHologram.MI_GlobeHologram"));
		if (FPackageName::DoesPackageExist(DefaultMaterialPath.GetLongPackageName()))
		{
			GlobeMaterial = Cast<UMaterialInterface>(DefaultMaterialPath.TryLoad());
		}
	}
	if (!IsTemplate() && World && !World->IsGameWorld())
	{
		// Same convenience for the Phase 2 assets (saved on the instance, so they are cooked).
		auto LoadDefault = [](const TCHAR* Path) -> UObject*
		{
			const FSoftObjectPath SoftPath(Path);
			return FPackageName::DoesPackageExist(SoftPath.GetLongPackageName()) ? SoftPath.TryLoad() : nullptr;
		};
		if (!SurfaceSystem)
		{
			SurfaceSystem = Cast<UNiagaraSystem>(LoadDefault(TEXT("/Game/AIGlobe/Niagara/NS_GlobeSurface.NS_GlobeSurface")));
		}
		if (!PointsMaterial)
		{
			PointsMaterial = Cast<UMaterialInterface>(LoadDefault(TEXT("/Game/AIGlobe/Materials/MI_GlobeParticle.MI_GlobeParticle")));
		}
		if (!PointsOnTopMaterial)
		{
			PointsOnTopMaterial = Cast<UMaterialInterface>(LoadDefault(TEXT("/Game/AIGlobe/Materials/MI_GlobeParticleOnTop.MI_GlobeParticleOnTop")));
		}
		if (Network && !Network->NetworkMaterial)
		{
			Network->NetworkMaterial = Cast<UMaterialInterface>(LoadDefault(TEXT("/Game/AIGlobe/Materials/MI_GlobeNetwork.MI_GlobeNetwork")));
		}
		if (!CloudWaveSystem)
		{
			CloudWaveSystem = Cast<UNiagaraSystem>(LoadDefault(TEXT("/Game/AIGlobe/Niagara/NS_GlobeCloudWave.NS_GlobeCloudWave")));
		}
		if (CloudRingCore && !CloudRingCore->CoreMaterial)
		{
			CloudRingCore->CoreMaterial = Cast<UMaterialInterface>(LoadDefault(TEXT("/Game/AIGlobe/Materials/MI_GlobeCloudCore.MI_GlobeCloudCore")));
		}
		if (!PrismsSystem)
		{
			PrismsSystem = Cast<UNiagaraSystem>(LoadDefault(TEXT("/Game/AIGlobe/Niagara/NS_GlobePrisms.NS_GlobePrisms")));
		}
		if (!CityLightsMaterial)
		{
			CityLightsMaterial = Cast<UMaterialInterface>(LoadDefault(TEXT("/Game/AIGlobe/Materials/MI_GlobeCityLights.MI_GlobeCityLights")));
		}
		if (PrismRibbon && !PrismRibbon->SheetMaterial)
		{
			PrismRibbon->SheetMaterial = Cast<UMaterialInterface>(LoadDefault(TEXT("/Game/AIGlobe/Materials/MI_GlobePrismSheet.MI_GlobePrismSheet")));
		}
	}
#endif

	// Runs on placement and every property edit in the editor, and on spawn at runtime.
	if (EnsureDynamicMaterial())
	{
		PushAllParameters();
	}
	RefreshSurface();
}

void AGlobeVFXActor::BeginPlay()
{
	Super::BeginPlay();
	PlacedGlobeRotation = GetActorQuat(); // Reset Globe Rotation returns here

	// PIE duplication and cooked loads do not carry the transient DMI over, so rebuild here.
	if (EnsureDynamicMaterial())
	{
		PushAllParameters();
	}
	ApplyFade();
	RefreshSurface();

	// Location/rotation follow through attachment; a scale change needs a re-fit (radius, sprite size, flow).
	if (GlobeMesh)
	{
		TransformUpdatedHandle = GlobeMesh->TransformUpdated.AddUObject(this, &AGlobeVFXActor::OnGlobeTransformUpdated);
	}

	// The ring's audio smoothing / test pattern runs in Tick.
	SetActorTickEnabled(NeedsTick());

	// Cards authored on this actor (Data Items). Code can replace them any time via GetDataLinks().
	if (DataItems.Num() > 0)
	{
		ApplyDataItems();
	}
}

void AGlobeVFXActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GlobeMesh)
	{
		GlobeMesh->TransformUpdated.Remove(TransformUpdatedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void AGlobeVFXActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(AGlobeVFXActor, DataItems))
	{
		ApplyDataItems(); // fills Ids from titles; live-updates the cards while playing
	}

	// In the editor world OnConstruction already re-applies everything. While playing it does not, so push
	// the new values here. (Stopping Play discards them; select the actor and press K to keep them.)
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (EnsureDynamicMaterial())
		{
			PushAllParameters();
		}
		RefreshSurface();
	}
}
#endif

void AGlobeVFXActor::OnGlobeTransformUpdated(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport)
{
	if (GlobeMesh && !GlobeMesh->GetComponentScale().Equals(LastRootScale, 1e-4))
	{
		RefreshSurface();
		// This fires before attached children (the earth in SurfaceFitActor) have updated their bounds, so the
		// fit above can still see the old earth size: re-fit once more next frame with the settled transforms.
		if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &AGlobeVFXActor::RefreshSurface);
		}
	}
}

void AGlobeVFXActor::ComputeSurfaceSphere(FVector& OutCenter, float& OutRadius, FQuat& OutRotation) const
{
	// Prefer the realistic mesh: its pivot is not at its centre, so use its bounds centre in world space.
	const UStaticMeshComponent* FitMesh = nullptr;
	if (SurfaceFitActor)
	{
		TArray<UStaticMeshComponent*> Meshes;
		SurfaceFitActor->GetComponents(Meshes);
		for (const UStaticMeshComponent* Candidate : Meshes)
		{
			if (Candidate->GetStaticMesh())
			{
				FitMesh = Candidate;
				break;
			}
		}
	}
	if (!FitMesh)
	{
		FitMesh = GlobeMesh;
	}

	const UStaticMesh* Mesh = FitMesh ? FitMesh->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		OutCenter = GetActorLocation();
		OutRadius = FMath::Max(1.0f, SurfaceRadiusOverride) + SurfaceOffset;
		OutRotation = GetActorQuat();
		return;
	}

	const FTransform& MeshTransform = FitMesh->GetComponentTransform();
	const FBoxSphereBounds LocalBounds = Mesh->GetBounds();
	OutRotation = MeshTransform.GetRotation(); // The land mask is baked in this mesh's local frame.

	if (bSpawnOnHologramSurface && GlobeMesh && GlobeMesh->GetStaticMesh())
	{
		// Exactly on the hologram shell: its centre and radius; rotation stays the earth's (land mask frame).
		const FTransform& ShellTransform = GlobeMesh->GetComponentTransform();
		const FBoxSphereBounds ShellBounds = GlobeMesh->GetStaticMesh()->GetBounds();
		OutCenter = ShellTransform.TransformPosition(ShellBounds.Origin);
		OutRadius = FMath::Max(1.0f, ShellBounds.SphereRadius * ShellTransform.GetScale3D().GetAbsMax());
		return;
	}

	OutCenter = MeshTransform.TransformPosition(LocalBounds.Origin);
	const float FittedRadius = SurfaceRadiusOverride > 0.0f
		? SurfaceRadiusOverride
		: LocalBounds.SphereRadius * MeshTransform.GetScale3D().GetAbsMax();
	const float ScaledRadius = FittedRadius * PointsRadiusScale;
	OutRadius = FMath::Max(1.0f, ScaledRadius + SurfaceOffset);
}

void AGlobeVFXActor::RefreshSurface()
{
	if (!GlobeMesh)
	{
		return;
	}

	FVector Center;
	FQuat Rotation;
	ComputeSurfaceSphere(Center, SurfaceRadius, Rotation);
	LastRootScale = GlobeMesh->GetComponentScale();

	if (SurfaceParticles)
	{
		SurfaceParticles->SetWorldLocationAndRotation(Center, Rotation);
		SurfaceParticles->SetWorldScale3D(FVector::OneVector);
		SurfaceParticles->SetTranslucentSortPriority(ParticleSortPriority);
		if (SurfaceSystem && SurfaceParticles->GetAsset() != SurfaceSystem)
		{
			SurfaceParticles->SetAsset(SurfaceSystem); // Only when the asset changes: swaps never reset the system.
		}
		SurfaceParticles->SetVisibility(bEnableSurfaceParticles);
		if (bEnableSurfaceParticles && !SurfaceParticles->IsActive())
		{
			SurfaceParticles->Activate();
		}
		else if (!bEnableSurfaceParticles && SurfaceParticles->IsActive())
		{
			SurfaceParticles->Deactivate();
		}

		float QualityScale = 1.0f;
		if (bScaleWithEffectsQuality)
		{
			static const float Scales[] = { 0.35f, 0.6f, 0.85f, 1.0f, 1.0f };
			QualityScale = Scales[FMath::Clamp(Scalability::GetQualityLevels().EffectsQuality, 0, 4)];
		}
		const float VisibleCount = ParticleCount * QualityScale;

		namespace NP = GlobeVFX::NiagaraParams;
		// Points spawn exactly on this sphere (Shape Location V2, Sphere Surface Distribution = 1 in NS_GlobeSurface).
		SurfaceParticles->SetVariableFloat(FName(NP::SurfaceRadius), SurfaceRadius);
		SurfaceParticles->SetVariableFloat(FName(NP::SurfaceHeightVariation), 0.0f); // clears values saved by older builds
		// Steady state = rate * lifetime. Land only: oceans are not drawn, so spawn more to keep the land covered.
		const float LandBoost = bPointsOnLandOnly ? LandPointsDensity : 1.0f;
		SurfaceParticles->SetVariableFloat(FName(NP::SpawnRate), VisibleCount * LandBoost / ParticleLifetime);
		SurfaceParticles->SetVariableFloat(FName(NP::ParticleLifetime), ParticleLifetime);
		SurfaceParticles->SetVariableLinearColor(FName(NP::GlowColor), ParticleColor);
		SurfaceParticles->SetVariableFloat(FName(NP::LandMaskStrength), bPointsOnLandOnly ? 1.0f : LandMaskStrength);
		// Fallback for levels saved before these slots existed (the editor auto-fills and saves them).
		if (!PointsMaterial)
		{
			PointsMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AIGlobe/Materials/MI_GlobeParticle.MI_GlobeParticle"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!PointsOnTopMaterial)
		{
			PointsOnTopMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AIGlobe/Materials/MI_GlobeParticleOnTop.MI_GlobeParticleOnTop"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		// Points On Land Only: instances of the points materials with the ocean brightness at 0.
		auto MakeLandOnly = [this](TObjectPtr<UMaterialInstanceDynamic>& MID, UMaterialInterface* Parent)
		{
			if (Parent && (!MID || MID->Parent != Parent))
			{
				MID = UMaterialInstanceDynamic::Create(Parent, this);
				MID->SetFlags(RF_Transient);
			}
			if (MID)
			{
				MID->SetScalarParameterValue(GlobeVFX::Params::OceanBrightness, 0.0f);
			}
		};
		if (bPointsOnLandOnly)
		{
			MakeLandOnly(LandPointsMID, PointsMaterial);
			MakeLandOnly(LandPointsOnTopMID, PointsOnTopMaterial);
		}
		AppliedSpriteMaterial = nullptr; // Re-apply below (the component may have been reset).
	}

	// Centre clearing. A point at fraction f of the disc radius (seen from afar) faces the camera with
	// facing = sqrt(1 - f^2), so the size maps to a facing threshold.
	const float ClearThreshold = FMath::Sqrt(FMath::Max(0.0f, 1.0f - FMath::Square(CenterClearSize)));
	if (SurfaceParticles)
	{
		namespace NP = GlobeVFX::NiagaraParams;
		SurfaceParticles->SetVariableFloat(FName(NP::CenterClearAmount), CenterClearAmount);
		SurfaceParticles->SetVariableFloat(FName(NP::CenterClearThreshold), ClearThreshold);
		SurfaceParticles->SetVariableFloat(FName(NP::CenterClearSoftness), CenterClearSoftness);
	}

	// Heights in cm (network, ring) are measured from the globe itself, not from the points (Points Height above it).
	const float GlobeSurfaceRadius = bSpawnOnHologramSurface ? SurfaceRadius : FMath::Max(1.0f, SurfaceRadius - SurfaceOffset);

	if (Network)
	{
		Network->SetWorldLocationAndRotation(Center, Rotation);
		Network->SetWorldScale3D(FVector::OneVector);
		Network->SetVisibility(bEnableNetwork);
		if (bEnableNetwork)
		{
			Network->Rebuild(SurfaceRadius, GlobeSurfaceRadius);
			Network->SetCenterClear(bCenterClearNetwork ? CenterClearAmount : 0.0f, ClearThreshold, CenterClearSoftness);
		}
	}

	RefreshCloudRing(Center, Rotation, GlobeSurfaceRadius);
	RefreshPrisms(Center, Rotation, GlobeSurfaceRadius);
	RefreshCityLights(Center, Rotation);
	PushDynamicParticleParameters();
	SetActorTickEnabled(NeedsTick());
}

void AGlobeVFXActor::PushDynamicParticleParameters()
{
	PushRingLook(); // The ring's opacity follows the S crossfade too.
	PushPrismLook();
	PushCityLightsLook(); // earth-mode fade follows the S crossfade
	// On-top points only while the earth is shown (switch at the crossfade midpoint); hologram mode keeps the
	// normal depth-tested look.
	if (SurfaceParticles)
	{
		const bool bOnTop = bDrawPointsOnTop && EarthModeBlend > 0.5f;
		UMaterialInterface* Sprite = bOnTop ? PointsOnTopMaterial.Get() : PointsMaterial.Get();
		if (bPointsOnLandOnly)
		{
			UMaterialInterface* LandOnly = bOnTop ? LandPointsOnTopMID.Get() : LandPointsMID.Get();
			Sprite = LandOnly ? LandOnly : Sprite;
		}
		if (Sprite && Sprite != AppliedSpriteMaterial)
		{
			SurfaceParticles->SetVariableMaterial(FName(GlobeVFX::NiagaraParams::SpriteMaterial), Sprite);
			AppliedSpriteMaterial = Sprite;
		}
	}
	// With bChangeParticlesOnSwap off, the swap only toggles the globe models; the layers keep the VFX-mode look.
	const float ModeBlend = bChangeParticlesOnSwap ? EarthModeBlend : 0.0f;
	if (SurfaceParticles)
	{
		namespace NP = GlobeVFX::NiagaraParams;
		const float FlowDegrees = FMath::Lerp(IdleFlowDegreesPerSecond, EnergizedFlowDegreesPerSecond, Energy);
		SurfaceParticles->SetVariableFloat(FName(NP::GlobalEnergy), Energy);
		SurfaceParticles->SetVariableFloat(FName(NP::ParticleIntensity), FMath::Lerp(VFXModeParticleIntensity, EarthModeParticleIntensity, ModeBlend));
		SurfaceParticles->SetVariableFloat(FName(NP::SpriteSize), SurfaceRadius * SpriteSizeFactor);
		float BackSide = bFrontPointsOnly ? 0.0f : FMath::Lerp(VFXModeBackSideVisibility, EarthModeBackSideVisibility, ModeBlend);
		if (bDrawPointsOnTop)
		{
			// On-top points ignore depth, so the earth cannot hide far-side points: fade them out as it appears.
			BackSide *= 1.0f - EarthModeBlend;
		}
		SurfaceParticles->SetVariableFloat(FName(NP::BackSideVisibility), BackSide);
		SurfaceParticles->SetVariableFloat(FName(NP::FlowSpeed), FMath::DegreesToRadians(FlowDegrees) * SurfaceRadius); // cm/s at the equator
	}
	// Hologram centre clears as the earth appears (visible only when the pawn keeps the hologram on the swap).
	const UWorld* World = GetWorld();
	const bool bEditorPreview = bPreviewEarthModeInEditor && World && !World->IsGameWorld();
	if (UMaterialInstanceDynamic* MID = DynamicMaterial)
	{
		MID->SetScalarParameterValue(GlobeVFX::Params::CenterClearAmount, HologramCenterClear * (bEditorPreview ? 1.0f : EarthModeBlend));
		MID->SetScalarParameterValue(GlobeVFX::Params::CenterClearThreshold, FMath::Sqrt(FMath::Max(0.0f, 1.0f - FMath::Square(HologramCenterClearSize))));
		MID->SetScalarParameterValue(GlobeVFX::Params::CenterClearSoftness, HologramCenterClearSoftness);
	}
	PushHologramLand(); // continents fade out as the earth fades in
	if (Network && bEnableNetwork)
	{
		const float ModeIntensity = FMath::Lerp(VFXModeNetworkIntensity, EarthModeNetworkIntensity, ModeBlend);
		Network->SetNetworkIntensity(ModeIntensity * FMath::Lerp(0.7f, 1.3f, Energy));
	}
}

void AGlobeVFXActor::CenterEarthInHologram()
{
	const UStaticMeshComponent* EarthMesh = SurfaceFitActor ? SurfaceFitActor->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	if (!EarthMesh || !EarthMesh->GetStaticMesh() || !GlobeMesh || !GlobeMesh->GetStaticMesh())
	{
		UE_LOG(LogGlobeVFX, Warning, TEXT("%s: set Surface Fit Actor to the earth mesh actor first."), *GetName());
		return;
	}
	const FVector EarthCenter = EarthMesh->GetComponentTransform().TransformPosition(EarthMesh->GetStaticMesh()->GetBounds().Origin);
	const FVector HologramCenter = GlobeMesh->GetComponentTransform().TransformPosition(GlobeMesh->GetStaticMesh()->GetBounds().Origin);
	SurfaceFitActor->Modify();
	SurfaceFitActor->AddActorWorldOffset(HologramCenter - EarthCenter);
	UE_LOG(LogGlobeVFX, Display, TEXT("%s: moved %s by %s to centre it in the hologram."), *GetName(), *SurfaceFitActor->GetActorNameOrLabel(), *(HologramCenter - EarthCenter).ToString());
	RefreshSurface();
}

void AGlobeVFXActor::SetEarthModeBlend(float Blend)
{
	const float Clamped = FMath::Clamp(Blend, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(Clamped, EarthModeBlend))
	{
		EarthModeBlend = Clamped;
		PushDynamicParticleParameters();
	}
}

UMaterialInstanceDynamic* AGlobeVFXActor::EnsureDynamicMaterial()
{
	if (!GlobeMesh)
	{
		return nullptr;
	}

	if (!GlobeMaterial)
	{
		DynamicMaterial = nullptr;
		GlobeMesh->SetMaterial(0, nullptr);
		return nullptr;
	}

	const bool bNeedsNewInstance = !DynamicMaterial || DynamicMaterial->Parent != GlobeMaterial;
	if (bNeedsNewInstance)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(GlobeMaterial, this);
		if (!DynamicMaterial)
		{
			UE_LOG(LogGlobeVFX, Warning, TEXT("%s: failed to create dynamic material from %s"), *GetName(), *GetNameSafe(GlobeMaterial));
			return nullptr;
		}
		DynamicMaterial->SetFlags(RF_Transient);
	}

	if (GlobeMesh->GetMaterial(0) != DynamicMaterial)
	{
		GlobeMesh->SetMaterial(0, DynamicMaterial);
	}
	return DynamicMaterial;
}

void AGlobeVFXActor::PushAllParameters()
{
	if (!DynamicMaterial)
	{
		return;
	}
	DynamicMaterial->SetVectorParameterValue(GlobeVFX::Params::GlobeColor, GlobeColor);
	DynamicMaterial->SetScalarParameterValue(GlobeVFX::Params::GlowIntensity, GlowIntensity);
	DynamicMaterial->SetScalarParameterValue(GlobeVFX::Params::Energy, Energy);
	DynamicMaterial->SetScalarParameterValue(GlobeVFX::Params::OpacityScale, HologramOpacity);
	DynamicMaterial->SetScalarParameterValue(GlobeVFX::Params::Fade, CurrentFade);
	PushHologramLand();
}

void AGlobeVFXActor::SetGlobeMaterial(UMaterialInterface* NewMaterial)
{
	GlobeMaterial = NewMaterial;
	if (EnsureDynamicMaterial())
	{
		PushAllParameters();
	}
}

void AGlobeVFXActor::SetGlobeColor(FLinearColor NewColor)
{
	GlobeColor = NewColor;
	if (UMaterialInstanceDynamic* MID = EnsureDynamicMaterial())
	{
		MID->SetVectorParameterValue(GlobeVFX::Params::GlobeColor, GlobeColor);
	}
}

void AGlobeVFXActor::SetGlowIntensity(float NewGlowIntensity)
{
	GlowIntensity = FMath::Max(0.0f, NewGlowIntensity);
	if (UMaterialInstanceDynamic* MID = EnsureDynamicMaterial())
	{
		MID->SetScalarParameterValue(GlobeVFX::Params::GlowIntensity, GlowIntensity);
	}
}

void AGlobeVFXActor::SetHologramOpacity(float NewOpacity)
{
	HologramOpacity = FMath::Clamp(NewOpacity, 0.0f, 2.0f);
	if (UMaterialInstanceDynamic* MID = EnsureDynamicMaterial())
	{
		MID->SetScalarParameterValue(GlobeVFX::Params::OpacityScale, HologramOpacity);
	}
}

void AGlobeVFXActor::SetEnergy(float NewEnergy)
{
	Energy = FMath::Clamp(NewEnergy, 0.0f, 1.0f);
	if (UMaterialInstanceDynamic* MID = EnsureDynamicMaterial())
	{
		MID->SetScalarParameterValue(GlobeVFX::Params::Energy, Energy);
	}
	PushDynamicParticleParameters(); // Particle brightness, twinkle rate and flow speed follow Energy.
}

void AGlobeVFXActor::FadeTo(float TargetFade, float Duration)
{
	FadeStart = CurrentFade;
	FadeTarget = FMath::Clamp(TargetFade, 0.0f, 1.0f);
	FadeElapsed = 0.0f;

	if (Duration <= UE_KINDA_SMALL_NUMBER || FMath::IsNearlyEqual(FadeStart, FadeTarget))
	{
		SetFade(FadeTarget);
		return;
	}

	FadeDuration = Duration;
	SetActorTickEnabled(true);
	ApplyFade(); // Makes the mesh visible straight away when fading in.
}

void AGlobeVFXActor::SetFade(float NewFade)
{
	CurrentFade = FMath::Clamp(NewFade, 0.0f, 1.0f);
	FadeDuration = 0.0f;
	SetActorTickEnabled(NeedsTick());
	ApplyFade();
}

bool AGlobeVFXActor::NeedsTick() const
{
	const UWorld* World = GetWorld();
	const bool bGame = World && World->IsGameWorld();
	const bool bRingAnimating = bEnableCloudRing && bGame;
	const bool bPrismsAnimating = bGame && (bEnablePrisms || PrismRamp > 0.0f); // wave clock + on/off ramp
	const bool bGlobeSpinning = bGame && !FMath::IsNearlyZero(GlobeSpinSpeed);
	const bool bCitySunTracking = bGame && bEnableCityLights && bCityLightsNightSideOnly; // follows the moving sun
	return FadeDuration > 0.0f || bRingAnimating || bPrismsAnimating || bGlobeSpinning || bCitySunTracking;
}

void AGlobeVFXActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdatePrisms(DeltaSeconds);
	if (bEnableCityLights)
	{
		UpdateCityLightsSun();
	}

	// Globe Spin turns the whole rig around its own axis; locked layers (absolute rotation) are unaffected.
	if (!FMath::IsNearlyZero(GlobeSpinSpeed))
	{
		AddActorLocalRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(GlobeSpinSpeed * DeltaSeconds)));
	}

	if (bEnableCloudRing)
	{
		UpdateRingEnergy(DeltaSeconds);
	}

	if (FadeDuration > 0.0f)
	{
		FadeElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(FadeElapsed / FadeDuration, 0.0f, 1.0f);
		CurrentFade = FMath::Lerp(FadeStart, FadeTarget, FMath::SmoothStep(0.0f, 1.0f, Alpha));
		if (Alpha >= 1.0f)
		{
			SetFade(FadeTarget); // Also re-evaluates whether Tick is still needed.
			return;
		}
		ApplyFade();
	}
	else if (!NeedsTick())
	{
		SetActorTickEnabled(false);
	}
}

// ------------------------------------------------------------------ Phase 3: cloud wave ring

float AGlobeVFXActor::SimulatedSpeech(float Time)
{
	// Phrases of ~3 s speech and ~1.2 s pause; ~4.6 syllables per second with a wandering accent.
	const float Phrase = FMath::Fmod(Time, 4.2f);
	const float Gate = FMath::SmoothStep(0.0f, 0.15f, Phrase) * (1.0f - FMath::SmoothStep(2.8f, 3.1f, Phrase));
	const float Syllable = FMath::Pow(FMath::Max(0.0f, FMath::Sin(UE_TWO_PI * 4.6f * Time)), 0.7f);
	const float Accent = 0.55f + 0.45f * FMath::Sin(UE_TWO_PI * 0.83f * Time + 1.3f) * FMath::Sin(UE_TWO_PI * 1.7f * Time + 0.4f);
	return FMath::Clamp(Gate * (0.12f + 0.88f * Syllable * Accent), 0.0f, 1.0f);
}

void AGlobeVFXActor::SetAudioEnergy(float NewAudioEnergy)
{
	AudioEnergyInput = FMath::Clamp(NewAudioEnergy, 0.0f, 1.0f);
}

void AGlobeVFXActor::SetTestAudio(EGlobeTestAudio Mode)
{
	TestAudio = Mode;
}

void AGlobeVFXActor::UpdateRingEnergy(float DeltaSeconds)
{
	float Target = AudioEnergyInput;
	switch (TestAudio)
	{
	case EGlobeTestAudio::Level0: Target = 0.0f; break;
	case EGlobeTestAudio::Level25: Target = 0.25f; break;
	case EGlobeTestAudio::Level50: Target = 0.5f; break;
	case EGlobeTestAudio::Level100: Target = 1.0f; break;
	case EGlobeTestAudio::SpeechPattern: Target = SimulatedSpeech(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f); break;
	default: break;
	}

	// Exponential smoothing with separate attack (rising) and release (falling) speeds.
	const float Speed = Target > SmoothedAudio ? RingAttackSpeed : RingReleaseSpeed;
	SmoothedAudio += (Target - SmoothedAudio) * (1.0f - FMath::Exp(-Speed * DeltaSeconds));

	// Slow spin of the mist around the ring axis (the core's waveform slides in its material at the same rate).
	if (CloudRingMist && !FMath::IsNearlyZero(RingSpinSpeed))
	{
		RingSpinAngle = FMath::Fmod(RingSpinAngle + RingSpinSpeed * DeltaSeconds, 360.0f);
		CloudRingMist->SetRelativeRotation(RingSpinBase * FQuat(FVector::UpVector, FMath::DegreesToRadians(RingSpinAngle)));
	}

	// The globe's own Energy adds a baseline on top of audio.
	RingEnergy = FMath::Clamp(SmoothedAudio + Energy * GlobalEnergyRingContribution, 0.0f, 1.0f);
	if (FMath::Abs(RingEnergy - LastPushedRingEnergy) > 0.002f)
	{
		PushRingLook();
	}
}

void AGlobeVFXActor::RefreshCloudRing(const FVector& Center, const FQuat& Rotation, float GlobeSurfaceRadius)
{
	RingGlobeRadius = GlobeSurfaceRadius;
	const float Radius = RingRadius > 0.0f ? RingRadius : GlobeSurfaceRadius * 1.15f;
	RingRadiusApplied = Radius;

	// Fallback for levels saved before these slots existed (the editor auto-fills and saves them).
	if (!CloudWaveSystem)
	{
		CloudWaveSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/AIGlobe/Niagara/NS_GlobeCloudWave.NS_GlobeCloudWave"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (CloudRingCore && !CloudRingCore->CoreMaterial)
	{
		CloudRingCore->CoreMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AIGlobe/Materials/MI_GlobeCloudCore.MI_GlobeCloudCore"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	// Orientation: the globe's equator, then tilted. When not following the globe's rotation, keep the globe
	// orientation from the start of play (in the editor the current one is the placement, so it is used directly).
	FQuat BaseRotation = Rotation;
	const UWorld* World = GetWorld();
	if (bLockVoiceRingRotation && World && World->IsGameWorld())
	{
		if (!bRingBaseRotationCaptured)
		{
			RingBaseRotation = Rotation;
			bRingBaseRotationCaptured = true;
		}
		BaseRotation = RingBaseRotation;
	}
	const FQuat RingRotation = BaseRotation * RingTilt.Quaternion();

	static const int32 Segments[] = { 64, 128, 256 };
	const int32 Quality = FMath::Clamp((int32)RingQuality, 0, 2);

	if (CloudRingCore)
	{
		// Absolute rotation = parent rotation is ignored, so rotating the globe leaves the ring as it is.
		CloudRingCore->SetUsingAbsoluteRotation(bLockVoiceRingRotation);
		CloudRingCore->SetWorldLocationAndRotation(Center, RingRotation);
		CloudRingCore->SetWorldScale3D(FVector::OneVector);
		const bool bCore = bEnableCloudRing && bShowRingCore; // the mist can stay without the core
		CloudRingCore->SetVisibility(bCore);
		if (bCore)
		{
			CloudRingCore->Rebuild(Radius, RingThickness, Segments[Quality]);
		}
	}

	if (CloudRingMist)
	{
		CloudRingMist->SetUsingAbsoluteRotation(bLockVoiceRingRotation);
		CloudRingMist->SetWorldLocationAndRotation(Center, RingRotation);
		// Spin is applied on top of this orientation (relative space works for both absolute and attached).
		RingSpinBase = CloudRingMist->GetRelativeRotation().Quaternion();
		CloudRingMist->SetRelativeRotation(RingSpinBase * FQuat(FVector::UpVector, FMath::DegreesToRadians(RingSpinAngle)));
		CloudRingMist->SetWorldScale3D(FVector::OneVector);
		const bool bMist = bEnableCloudRing && bShowCloudMist; // the core can stay without the mist
		CloudRingMist->SetVisibility(bMist);
		if (CloudWaveSystem && CloudRingMist->GetAsset() != CloudWaveSystem)
		{
			CloudRingMist->SetAsset(CloudWaveSystem); // Only when the asset changes: swaps never reset it.
		}
		if (bMist && !CloudRingMist->IsActive())
		{
			CloudRingMist->Activate();
		}
		else if (!bMist && CloudRingMist->IsActive())
		{
			CloudRingMist->Deactivate();
		}

		namespace CP = GlobeVFX::CloudParams;
		const float Tube = RingThickness * 0.35f;
		CloudRingMist->SetVariableFloat(FName(CP::RingRadius), Radius);
		CloudRingMist->SetVariableFloat(FName(CP::RingThickness), Tube);
		CloudRingMist->SetVariableFloat(FName(CP::MistSize), MistSize);
		CloudRingMist->SetVariableFloat(FName(CP::MistLifetime), 3.0f);
		CloudRingMist->SetVariableFloat(FName(CP::MistNoiseFrequency), 1.0f / FMath::Max(50.0f, RingThickness * 2.0f));
		CloudRingMist->SetVariableFloat(FName(CP::MistDrag), 1.5f);
		CloudRingMist->SetVariableLinearColor(FName(GlobeVFX::NiagaraParams::GlowColor), RingColor);
		// Fixed bounds (no per-frame bounds work): the torus plus room for drifting mist.
		const float Reach = Radius + Tube + MistSize + RingThickness;
		CloudRingMist->SetSystemFixedBounds(FBox(FVector(-Reach, -Reach, -RingThickness * 2.0f - MistSize), FVector(Reach, Reach, RingThickness * 2.0f + MistSize)));
	}

	LastPushedRingEnergy = -1.0f; // Force the look to re-push.
	PushRingLook();
}

void AGlobeVFXActor::PushRingLook()
{
	LastPushedRingEnergy = RingEnergy;
	// Ring opacity follows the S crossfade (always, independent of bChangeParticlesOnSwap).
	const float Intensity = bEnableCloudRing ? FMath::Lerp(VFXModeRingIntensity, EarthModeRingIntensity, EarthModeBlend) : 0.0f;

	// Centre clearing ramps in with the earth (same facing rule as the hologram / points).
	const float ClearAmount = RingCenterClear * EarthModeBlend;
	const float ClearThreshold = FMath::Sqrt(FMath::Max(0.0f, 1.0f - FMath::Square(RingCenterClearSize)));

	if (CloudRingCore)
	{
		CloudRingCore->SetLook(Intensity, RingEnergy, WaveHeight, RingColor);
		CloudRingCore->SetScalar(GlobeVFX::Params::RingSpin, RingSpinSpeed / 360.0f);
		CloudRingCore->SetScalar(GlobeVFX::Params::CenterClearAmount, ClearAmount);
		CloudRingCore->SetScalar(GlobeVFX::Params::CenterClearThreshold, ClearThreshold);
		CloudRingCore->SetScalar(GlobeVFX::Params::CenterClearSoftness, RingCenterClearSoftness);
	}
	if (CloudRingMist)
	{
		static const float SpawnScale[] = { 0.4f, 0.7f, 1.0f };
		const float Quality = SpawnScale[FMath::Clamp((int32)RingQuality, 0, 2)];
		namespace CP = GlobeVFX::CloudParams;
		CloudRingMist->SetVariableFloat(FName(CP::AudioEnergy), RingEnergy);
		CloudRingMist->SetVariableFloat(FName(CP::RingIntensity), Intensity);
		CloudRingMist->SetVariableFloat(FName(CP::CloudDensity), CloudDensity * Quality * FMath::Lerp(0.8f, 1.6f, RingEnergy));
		CloudRingMist->SetVariableFloat(FName(CP::MistTurbulence), FMath::Lerp(25.0f, 110.0f, RingEnergy));
		// The mist component itself turns at RingSpinSpeed (see UpdateRingEnergy); the swirl only adds a little
		// extra drift in the same direction as energy rises.
		CloudRingMist->SetVariableFloat(FName(CP::MistSwirl), FMath::Lerp(5.0f, 30.0f, RingEnergy) * (RingSpinSpeed < 0.0f ? -1.0f : 1.0f));
		CloudRingMist->SetVariableFloat(FName(CP::RingClearAmount), ClearAmount);
		CloudRingMist->SetVariableFloat(FName(CP::RingClearThreshold), ClearThreshold);
	}
}

// ------------------------------------------------------------------ Blue / gold waves

void AGlobeVFXActor::SetClothRibbonsEnabled(bool bEnabled)
{
	if (bClothRibbons != bEnabled)
	{
		bClothRibbons = bEnabled;
		RefreshSurface();
	}
}

void AGlobeVFXActor::SetCloudMistEnabled(bool bEnabled)
{
	if (bShowCloudMist != bEnabled)
	{
		bShowCloudMist = bEnabled;
		RefreshSurface();
	}
}

void AGlobeVFXActor::SetRingLineEnabled(bool bEnabled)
{
	if (bShowRingCore != bEnabled)
	{
		bShowRingCore = bEnabled;
		RefreshSurface();
	}
}

void AGlobeVFXActor::SetLayersEnabled(bool bSurfacePoints, bool bNetworkLayer, bool bCloudRingLayer, bool bPrismLayer)
{
	bEnableSurfaceParticles = bSurfacePoints;
	bEnableNetwork = bNetworkLayer;
	bEnableCloudRing = bCloudRingLayer;
	bEnablePrisms = bPrismLayer;
	RefreshSurface();
}

void AGlobeVFXActor::RefreshPrisms(const FVector& Center, const FQuat& Rotation, float GlobeSurfaceRadius)
{
	if (!Prisms || !PrismRibbon || !GlobeMesh)
	{
		return;
	}
	// Fallbacks for levels saved before these slots existed (the editor auto-fills and saves them).
	if (!PrismsSystem)
	{
		PrismsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/AIGlobe/Niagara/NS_GlobePrisms.NS_GlobePrisms"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!PrismRibbon->SheetMaterial)
	{
		PrismRibbon->SheetMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AIGlobe/Materials/MI_GlobePrismSheet.MI_GlobePrismSheet"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	// Radii in world cm. Both components have absolute scale 1, so every length below is applied once: the measured
	// radii already include the actor (and earth) scale, and the override is scaled here by the root scale only.
	const float RootScale = GlobeMesh->GetComponentScale().GetAbsMax();
	const UStaticMesh* ShellMesh = GlobeMesh->GetStaticMesh();
	const float ShellRadius = ShellMesh ? ShellMesh->GetBounds().SphereRadius * RootScale : 0.0f;
	const float GlobeRadius = FMath::Max3(ShellRadius, GlobeSurfaceRadius, 1.0f); // the visible globe (occluder)
	const float BaseRadius = FMath::Max(GlobeRadius, PrismGlobeRadius > 0.0f ? PrismGlobeRadius * RootScale : GlobeRadius) * FMath::Max(0.6f, PrismRadiusMultiplier);
	AppliedPrismRadius = BaseRadius;
	const float Inner = FMath::Max(GlobeRadius * 1.02f, BaseRadius * RibbonInnerRadius); // the band never reaches into the globe
	const float Outer = FMath::Max(Inner + BaseRadius * 0.05f, BaseRadius * RibbonOuterRadius);
	const float SizeScale = BaseRadius / 1150.0f; // sparkle size is authored for the 11.5 m showcase globe

	// Orientation: the globe's (or, like the ring, the one from the start of play), then the ribbon's tilt.
	FQuat BaseRotation = Rotation;
	const UWorld* World = GetWorld();
	if (bLockPrismRotation && World && World->IsGameWorld())
	{
		if (!bPrismBaseRotationCaptured)
		{
			PrismBaseRotation = Rotation;
			bPrismBaseRotationCaptured = true;
		}
		BaseRotation = PrismBaseRotation;
	}
	// Tilt about a horizontal axis pointing at Tilt Direction (in the globe's frame).
	const float TiltDirection = FMath::DegreesToRadians(PrismTiltDirection);
	const FQuat Tilt(FVector(FMath::Cos(TiltDirection), FMath::Sin(TiltDirection), 0.0f), FMath::DegreesToRadians(PrismTilt));
	const FQuat RibbonRotation = BaseRotation * Tilt;
	for (USceneComponent* Component : TArray<USceneComponent*>{ PrismRibbon, Prisms })
	{
		Component->SetUsingAbsoluteRotation(bLockPrismRotation);
		Component->SetWorldLocationAndRotation(Center, RibbonRotation);
		Component->SetWorldScale3D(FVector::OneVector);
		// Ribbon Spin is applied on top of this orientation (relative space works for both absolute and attached).
		PrismSpinBase = Component->GetRelativeRotation().Quaternion();
		Component->SetRelativeRotation(PrismSpinBase * FQuat(FVector::UpVector, FMath::DegreesToRadians(PrismSpinAngle)));
	}
	PrismRibbon->SetTranslucentSortPriority(ParticleSortPriority);
	Prisms->SetTranslucentSortPriority(ParticleSortPriority);
	if (PrismsSystem && Prisms->GetAsset() != PrismsSystem)
	{
		Prisms->SetAsset(PrismsSystem); // Only when the asset changes: swaps never reset it.
	}

	static const int32 Segments[] = { 128, 192, 256 };
	static const float SparkleScale[] = { 0.4f, 0.7f, 1.0f };
	const int32 Quality = FMath::Clamp((int32)PrismQuality, 0, 2);
	const int32 SheetCount = FMath::Clamp(RibbonLayers, 1, 8);
	PrismRibbon->Rebuild(Inner, Outer, SheetCount, Segments[Quality], bClothRibbons && RibbonTrailTime > 0.0f ? RibbonTrailEchoes : 0);
	// Room for the tallest wave (energy 1.3x, sheet offset 0.075) above and below the flat mesh.
	const float MaxAmp = RibbonWaveHeight * BaseRadius * 1.3f * (1.0f + (bClothRibbons ? RibbonFlutter : 0.0f)); // flutter adds up to its own height
	PrismRibbon->SetWaveBounds(MaxAmp * 1.1f + 10.0f);

	namespace PP = GlobeVFX::PrismParams;
	auto SetBoth = [this](const TCHAR* Name, float Value)
	{
		PrismRibbon->SetScalar(FName(Name), Value);
		Prisms->SetVariableFloat(FName(Name), Value);
	};
	SetBoth(PP::RibbonInner, Inner);
	SetBoth(PP::RibbonOuter, Outer);
	SetBoth(PP::RibbonLayers, (float)SheetCount);
	SetBoth(PP::OccluderRadius, GlobeRadius);
	SetBoth(PP::FrontVisibility, bPrismFullCircle ? 1.0f : PrismFrontVisibility); // full circle: one connected ring
	PrismRibbon->SetScalar(FName(PP::RibbonEdgeGlow), RibbonEdgeGlow);
	PrismRibbon->SetScalar(FName(PP::RibbonFill), RibbonFill);
	PrismRibbon->SetScalar(FName(PP::RibbonLineGlow), RibbonLineGlow);
	PrismRibbon->SetScalar(FName(PP::RibbonLineWidth), RibbonLineWidth);
	PrismRibbon->SetScalar(FName(PP::RibbonLineHalo), RibbonLineHalo);
	PrismRibbon->SetScalar(FName(PP::RibbonTrailOpacity), RibbonTrailOpacity);
	SetBoth(PP::RibbonFlutter, bClothRibbons ? RibbonFlutter : 0.0f); // the sparkles ride the same fluttering surface
	PrismRibbon->SetVector(FName(PP::BlueColor), BlueColor);
	PrismRibbon->SetVector(FName(PP::GoldColor), GoldColor);
	Prisms->SetVariableLinearColor(FName(PP::BlueColor), BlueColor);
	Prisms->SetVariableLinearColor(FName(PP::GoldColor), GoldColor);
	Prisms->SetVariableFloat(FName(PP::SparkleRate), 60.0f * SparkleAmount * SparkleScale[Quality]);
	Prisms->SetVariableFloat(FName(PP::SparkleLifetime), 3.0f);
	Prisms->SetVariableFloat(FName(PP::SparkleSize), SparkleSize * SizeScale);
	Prisms->SetVariableFloat(FName(PP::SparkleDrift), 0.01f);
	Prisms->SetVariableFloat(FName(PP::SparkleIntensity), 2.5f);
	// Fixed bounds (no per-frame bounds work, never culled while on screen): the band plus the waves and sparkles.
	const float Reach = Outer + SparkleSize * SizeScale * 2.0f;
	const float Height = MaxAmp * 1.5f + SparkleSize * SizeScale * 2.0f;
	Prisms->SetSystemFixedBounds(FBox(FVector(-Reach, -Reach, -Height), FVector(Reach, Reach, Height)));

	ApplyPrismActivation();
	PushPrismLook();
}

void AGlobeVFXActor::ApplyPrismActivation()
{
	if (!Prisms || !PrismRibbon)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		PrismRamp = bEnablePrisms ? 1.0f : 0.0f; // editor preview: no ramp
	}
	// Stays alive while ramping down; only stops once fully off, so switching back on mid-ramp just reverses.
	const bool bRunning = bEnablePrisms || PrismRamp > 0.0f;
	PrismRibbon->SetVisibility(bRunning);
	Prisms->SetVisibility(bRunning);
	if (bRunning && !Prisms->IsActive())
	{
		Prisms->Activate(true); // fresh start; the ramp grows it in from its min scale
	}
	else if (!bRunning && Prisms->IsActive())
	{
		Prisms->DeactivateImmediate();
	}
}

void AGlobeVFXActor::SetPrismShape(bool bFullCircle, float Tilt, float TiltDirection, float Size, float Intensity)
{
	bPrismFullCircle = bFullCircle;
	PrismTilt = Tilt;
	PrismTiltDirection = TiltDirection;
	PrismRadiusMultiplier = FMath::Clamp(Size, 0.6f, 3.0f);
	PrismIntensity = FMath::Max(0.0f, Intensity);
	RefreshSurface(); // re-places and re-sizes the ribbon; the wave clock and sparkles keep running
}

void AGlobeVFXActor::SetPrismsOn(bool bOn)
{
	bEnablePrisms = bOn;
	ApplyPrismActivation();
	PushPrismLook();
	if (NeedsTick())
	{
		SetActorTickEnabled(true); // the ramp and the wave clock run in Tick
	}
}

void AGlobeVFXActor::UpdatePrisms(float DeltaSeconds)
{
	if (!bEnablePrisms && PrismRamp <= 0.0f)
	{
		return;
	}
	// Energy is smoothed so speed, brightness and wave height ramp instead of stepping.
	SmoothedPrismEnergy = SmoothedPrismEnergy < 0.0f ? Energy : FMath::Lerp(SmoothedPrismEnergy, Energy, 1.0f - FMath::Exp(-2.5f * DeltaSeconds));
	// One clock for the sheet material and the sparkles. Changing the speed changes its rate, never its value, so the
	// waves never jump; it wraps at ClockPeriod, where every wave has completed whole cycles.
	const double Period = GlobeVFX::PrismParams::ClockPeriod;
	PrismClockRate = PrismSpeed * (bClothRibbons ? ClothSpeedMultiplier : 1.0f) * FMath::Lerp(1.0f, 2.0f, SmoothedPrismEnergy);
	PrismClock = FMath::Fmod(PrismClock + DeltaSeconds * PrismClockRate, Period);
	// The cloth flutter runs on its own, faster clock (same wrap rule).
	FlutterClockRate = bClothRibbons ? FlutterSpeed * FMath::Lerp(1.0f, 1.5f, SmoothedPrismEnergy) : 0.0f;
	FlutterClock = FMath::Fmod(FlutterClock + DeltaSeconds * FlutterClockRate, Period);
	// Ribbon Spin: turn the ring (and its sparkles) around its own axis on top of the tilt.
	if (!FMath::IsNearlyZero(PrismSpinSpeed))
	{
		PrismSpinAngle = FMath::Fmod(PrismSpinAngle + PrismSpinSpeed * DeltaSeconds, 360.0f);
		const FQuat Spun = PrismSpinBase * FQuat(FVector::UpVector, FMath::DegreesToRadians(PrismSpinAngle));
		PrismRibbon->SetRelativeRotation(Spun);
		Prisms->SetRelativeRotation(Spun);
	}

	const float Target = bEnablePrisms ? 1.0f : 0.0f;
	if (PrismRamp != Target)
	{
		const float Step = PrismRampDuration > UE_KINDA_SMALL_NUMBER ? DeltaSeconds / PrismRampDuration : 1.0f;
		PrismRamp = Target > PrismRamp ? FMath::Min(Target, PrismRamp + Step) : FMath::Max(Target, PrismRamp - Step);
	}
	PushPrismLook();
	if (PrismRamp <= 0.0f && !bEnablePrisms)
	{
		ApplyPrismActivation(); // fully off: hide the ribbon, stop the sparkles
	}
}

void AGlobeVFXActor::PushPrismLook()
{
	if (!Prisms || !PrismRibbon)
	{
		return;
	}
	// Brightness and the covered-part rule follow the S crossfade; the ribbon's shape is never touched by it.
	namespace PP = GlobeVFX::PrismParams;
	const float EnergyNow = SmoothedPrismEnergy < 0.0f ? Energy : SmoothedPrismEnergy;
	const float ModeIntensity = FMath::Lerp(VFXModePrismIntensity, EarthModePrismIntensity, EarthModeBlend);
	auto SetBoth = [this](const TCHAR* Name, float Value)
	{
		PrismRibbon->SetScalar(FName(Name), Value);
		Prisms->SetVariableFloat(FName(Name), Value);
	};
	SetBoth(PP::RibbonClock, (float)PrismClock);
	SetBoth(PP::RibbonFlutterClock, (float)FlutterClock);
	// Trail: the last echo runs Trail Length seconds behind, on each clock at its current rate.
	PrismRibbon->SetScalar(FName(PP::RibbonTrailLag), RibbonTrailTime * PrismClockRate);
	PrismRibbon->SetScalar(FName(PP::RibbonTrailFlutterLag), RibbonTrailTime * FlutterClockRate);
	SetBoth(PP::RibbonAmp, RibbonWaveHeight * AppliedPrismRadius * FMath::Lerp(1.0f, 1.3f, EnergyNow));
	SetBoth(PP::PrismIntensity, PrismIntensity * ModeIntensity * FMath::Lerp(0.75f, 1.35f, EnergyNow));
	SetBoth(PP::BackSideVisibility, FMath::Lerp(VFXModePrismBackSide, EarthModePrismBackSide, EarthModeBlend));
	SetBoth(PP::PrismRamp, FMath::SmoothStep(0.0f, 1.0f, PrismRamp));
	SetBoth(PP::RampMinScale, PrismRampMinScale);
}

void AGlobeVFXActor::ApplyFade()
{
	if (UMaterialInstanceDynamic* MID = EnsureDynamicMaterial())
	{
		MID->SetScalarParameterValue(GlobeVFX::Params::Fade, CurrentFade);
	}

	// Only toggle visibility in a running game so the editor view is never altered.
	const UWorld* World = GetWorld();
	if (!GlobeMesh || !World || !World->IsGameWorld())
	{
		return;
	}

	const bool bFadingIn = FadeDuration > 0.0f && FadeTarget > 0.0f;
	GlobeMesh->SetVisibility(CurrentFade > 0.0f || bFadingIn);
}

void AGlobeVFXActor::ResetGlobeRotation()
{
	SetActorRotation(PlacedGlobeRotation);
}

// ------------------------------------------------------------------ City lights

void AGlobeVFXActor::RefreshCityLights(const FVector& Center, const FQuat& Rotation)
{
	if (!CityLights)
	{
		return;
	}
	// Fallback for levels saved before this slot existed (the editor auto-fills and saves it).
	if (!CityLightsMaterial)
	{
		CityLightsMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AIGlobe/Materials/MI_GlobeCityLights.MI_GlobeCityLights"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	// The land mask belongs to the earth mesh, so the shell needs it: no earth (Surface Fit Actor) = no city lights.
	const UStaticMeshComponent* Earth = FindEarthMesh();
	const bool bShow = bEnableCityLights && Earth && CityLightsMaterial && CityLights->GetStaticMesh();
	CityLights->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	// Centre and rotation are the earth's (ComputeSurfaceSphere), so the shell's local frame is the land mask's frame.
	// Just above the surface: the engine sphere's facets dip ~0.5% between vertices, the earth's relief is ~0.3%.
	const float EarthRadius = Earth->GetStaticMesh()->GetBounds().SphereRadius * Earth->GetComponentScale().GetAbsMax();
	const float ShellMeshRadius = FMath::Max(1.0f, CityLights->GetStaticMesh()->GetBounds().SphereRadius);
	CityLights->SetWorldLocationAndRotation(Center, Rotation);
	CityLights->SetWorldScale3D(FVector(EarthRadius * 1.012f / ShellMeshRadius));
	CityLights->SetTranslucentSortPriority(ParticleSortPriority);
	if (!CityLightsMID || CityLightsMID->Parent != CityLightsMaterial)
	{
		CityLightsMID = UMaterialInstanceDynamic::Create(CityLightsMaterial, this);
		CityLightsMID->SetFlags(RF_Transient);
	}
	if (CityLights->GetMaterial(0) != CityLightsMID)
	{
		CityLights->SetMaterial(0, CityLightsMID);
	}
	LastSunDirection = FVector(1.0e9); // force the sun to be re-pushed
	PushCityLightsLook();
	UpdateCityLightsSun();
}

void AGlobeVFXActor::PushCityLightsLook()
{
	UMaterialInstanceDynamic* MID = CityLightsMID;
	if (!MID)
	{
		return;
	}
	namespace P = GlobeVFX::Params;
	MID->SetScalarParameterValue(P::CityIntensity, CityLightsIntensity);
	MID->SetScalarParameterValue(P::CityDensity, CityLightsDensity);
	MID->SetScalarParameterValue(P::CitySizeMin, CityLightsSizeMin);
	MID->SetScalarParameterValue(P::CitySizeMax, CityLightsSizeMax);
	MID->SetScalarParameterValue(P::CityBigShare, CityLightsBigShare);
	MID->SetScalarParameterValue(P::CityHalo, CityLightsHalo);
	MID->SetScalarParameterValue(P::CityScale, CityLightsScale);
	MID->SetScalarParameterValue(P::CityBlink, CityLightsBlink);
	MID->SetScalarParameterValue(P::CityBlinkSpeed, CityLightsBlinkSpeed);
	MID->SetScalarParameterValue(P::CityGlow, CityLightsGlow);
	MID->SetScalarParameterValue(P::CityNightSoftness, CityLightsNightSoftness);
	MID->SetScalarParameterValue(P::CityNightDarkness, CityNightDarkness);
	MID->SetVectorParameterValue(P::CityColor, CityLightsColor);
	MID->SetVectorParameterValue(P::CityColorB, CityLightsColorB);
	// Earth mode by default (the earth is hidden in hologram mode); follows the S crossfade.
	MID->SetScalarParameterValue(P::CityFade, bEnableCityLights ? FMath::Lerp(VFXModeCityLights, EarthModeCityLights, EarthModeBlend) : 0.0f);
}

void AGlobeVFXActor::UpdateCityLightsSun()
{
	if (!CityLightsMID)
	{
		return;
	}
	// The sun: the level's Directional Light (the atmosphere sun, driven by Globe Time Of Day).
	if (!CachedSun.IsValid())
	{
		if (TActorIterator<ADirectionalLight> It(GetWorld()); It)
		{
			CachedSun = *It;
		}
	}
	// Zero = no sun known (or night side disabled): the material lights every land area.
	const FVector ToSun = (bCityLightsNightSideOnly && CachedSun.IsValid()) ? -CachedSun->GetActorForwardVector() : FVector::ZeroVector;
	if (!ToSun.Equals(LastSunDirection, 1.0e-4))
	{
		LastSunDirection = ToSun;
		CityLightsMID->SetVectorParameterValue(GlobeVFX::Params::SunDirection, FLinearColor(ToSun.X, ToSun.Y, ToSun.Z, 0.0f));
	}
}

// ------------------------------------------------------------------ Hologram continents

const UStaticMeshComponent* AGlobeVFXActor::FindEarthMesh() const
{
	if (!SurfaceFitActor)
	{
		return nullptr;
	}
	TArray<UStaticMeshComponent*> Meshes;
	SurfaceFitActor->GetComponents(Meshes);
	for (const UStaticMeshComponent* Candidate : Meshes)
	{
		if (Candidate->GetStaticMesh())
		{
			return Candidate;
		}
	}
	return nullptr;
}

void AGlobeVFXActor::PushHologramLand()
{
	UMaterialInstanceDynamic* MID = DynamicMaterial;
	if (!MID || !GlobeMesh)
	{
		return;
	}
	namespace P = GlobeVFX::Params;
	// The land mask is in the earth mesh's local frame: give the shell the earth's axes in the shell's own local
	// space. Both are attached to this actor, so this stays valid however the globe is rotated.
	const UStaticMeshComponent* Earth = FindEarthMesh();
	if (Earth)
	{
		const FQuat EarthInShell = GlobeMesh->GetComponentQuat().Inverse() * Earth->GetComponentQuat();
		auto Axis = [&EarthInShell](const FVector& V) { const FVector A = EarthInShell.RotateVector(V); return FLinearColor(A.X, A.Y, A.Z, 0.0f); };
		MID->SetVectorParameterValue(P::LandAxisX, Axis(FVector::XAxisVector));
		MID->SetVectorParameterValue(P::LandAxisY, Axis(FVector::YAxisVector));
		MID->SetVectorParameterValue(P::LandAxisZ, Axis(FVector::ZAxisVector));
	}
	// Shown in hologram mode, gone in earth mode (follows the S crossfade, like the editor's earth-mode preview).
	const UWorld* World = GetWorld();
	const bool bEditorPreview = bPreviewEarthModeInEditor && World && !World->IsGameWorld();
	const float EarthShown = bEditorPreview ? 1.0f : EarthModeBlend;
	MID->SetScalarParameterValue(P::LandFade, (bHologramShowsLand && Earth) ? 1.0f - EarthShown : 0.0f);
	MID->SetVectorParameterValue(P::LandColor, HologramLandColor);
	MID->SetScalarParameterValue(P::LandFill, HologramLandFill);
	MID->SetScalarParameterValue(P::LandOpacity, HologramLandOpacity);
	MID->SetScalarParameterValue(P::LandCoastGlow, HologramCoastGlow);
}
