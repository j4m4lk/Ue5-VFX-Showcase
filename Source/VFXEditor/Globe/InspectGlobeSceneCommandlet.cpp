// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/InspectGlobeSceneCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "NiagaraGraph.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeInspect, Display, All);

UInspectGlobeSceneCommandlet::UInspectGlobeSceneCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace
{
	void InspectMesh(const FString& MeshPath)
	{
		UStaticMesh* Mesh = nullptr;
		if (MeshPath.StartsWith(TEXT("/")))
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		}
		else
		{
			// Look up by asset name (paths may contain characters that do not survive the command line).
			IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			Registry.SearchAllAssets(true);
			TArray<FAssetData> Found;
			Registry.GetAssetsByClass(UStaticMesh::StaticClass()->GetClassPathName(), Found);
			for (const FAssetData& Asset : Found)
			{
				if (Asset.AssetName.ToString() == MeshPath)
				{
					Mesh = Cast<UStaticMesh>(Asset.GetAsset());
					break;
				}
			}
		}
		if (!Mesh)
		{
			UE_LOG(LogGlobeInspect, Error, TEXT("Mesh not found: %s"), *MeshPath);
			return;
		}

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		UE_LOG(LogGlobeInspect, Display, TEXT("MESH %s: BoundsOrigin=%s Extent=%s SphereRadius=%.3f LODs=%d"),
			*Mesh->GetName(), *Bounds.Origin.ToString(), *Bounds.BoxExtent.ToString(), Bounds.SphereRadius, Mesh->GetNumSourceModels());

		UE_LOG(LogGlobeInspect, Display, TEXT("  Nanite enabled=%d"), Mesh->NaniteSettings.bEnabled ? 1 : 0);
		const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			UMaterialInterface* Mat = Materials[i].MaterialInterface;
			UE_LOG(LogGlobeInspect, Display, TEXT("  Slot %d '%s' -> %s"), i, *Materials[i].MaterialSlotName.ToString(), *GetPathNameSafe(Mat));
			if (UMaterialInstance* MI = Cast<UMaterialInstance>(Mat))
			{
				for (const FTextureParameterValue& Tex : MI->TextureParameterValues)
				{
					const UTexture* T = Tex.ParameterValue;
					UE_LOG(LogGlobeInspect, Display, TEXT("     tex %s = %s (%.0fx%.0f sRGB=%d)"), *Tex.ParameterInfo.Name.ToString(), *GetPathNameSafe(T),
						T ? T->GetSurfaceWidth() : 0.f, T ? T->GetSurfaceHeight() : 0.f, T ? T->SRGB : -1);
				}
			}
		}

		const FMeshDescription* Desc = Mesh->GetMeshDescription(0);
		if (!Desc)
		{
			UE_LOG(LogGlobeInspect, Warning, TEXT("  No mesh description for LOD0"));
			return;
		}

		FStaticMeshConstAttributes Attributes(*Desc);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		const TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		const TPolygonGroupAttributesConstRef<FName> GroupNames = Attributes.GetPolygonGroupMaterialSlotNames();
		UE_LOG(LogGlobeInspect, Display, TEXT("  Vertices=%d Instances=%d Triangles=%d UVChannels=%d PolygonGroups=%d"),
			Desc->Vertices().Num(), Desc->VertexInstances().Num(), Desc->Triangles().Num(), UVs.GetNumChannels(), Desc->PolygonGroups().Num());

		// Radius spread around the bounds origin tells us whether it is really a sphere and how it is pivoted.
		const FVector3f Center(Bounds.Origin);
		float MinR = TNumericLimits<float>::Max(), MaxR = 0.f;
		double SumR = 0.0;
		for (const FVertexID V : Desc->Vertices().GetElementIDs())
		{
			const float R = (Positions[V] - Center).Size();
			MinR = FMath::Min(MinR, R);
			MaxR = FMath::Max(MaxR, R);
			SumR += R;
		}
		UE_LOG(LogGlobeInspect, Display, TEXT("  Radius about bounds origin: min=%.3f max=%.3f mean=%.3f"), MinR, MaxR, SumR / FMath::Max(1, Desc->Vertices().Num()));

		// Per polygon group: UV range and a few direction->UV samples to identify the projection.
		for (const FPolygonGroupID Group : Desc->PolygonGroups().GetElementIDs())
		{
			FVector2f UVMin(TNumericLimits<float>::Max()), UVMax(-TNumericLimits<float>::Max());
			int32 Sampled = 0;
			int32 Count = 0;
			const TArrayView<const FTriangleID> Tris = Desc->GetPolygonGroupTriangles(Group);
			const int32 Step = FMath::Max(1, Tris.Num() / 8);
			for (int32 t = 0; t < Tris.Num(); ++t)
			{
				for (const FVertexInstanceID VI : Desc->GetTriangleVertexInstances(Tris[t]))
				{
					const FVector2f UV = UVs.Get(VI, 0);
					UVMin = FVector2f::Min(UVMin, UV);
					UVMax = FVector2f::Max(UVMax, UV);
					++Count;
				}
				if (t % Step == 0 && Sampled < 8)
				{
					const FVertexInstanceID VI = Desc->GetTriangleVertexInstances(Tris[t])[0];
					const FVector3f Dir = (Positions[Desc->GetVertexInstanceVertex(VI)] - Center).GetSafeNormal();
					const float Lon = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
					const float Lat = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Dir.Z, -1.f, 1.f)));
					const FVector2f UV = UVs.Get(VI, 0);
					UE_LOG(LogGlobeInspect, Display, TEXT("     sample dir=(%.3f,%.3f,%.3f) lon=%.1f lat=%.1f uv=(%.4f,%.4f)"), Dir.X, Dir.Y, Dir.Z, Lon, Lat, UV.X, UV.Y);
					++Sampled;
				}
			}
			UE_LOG(LogGlobeInspect, Display, TEXT("  Group %d '%s': tris=%d UV min=(%.4f,%.4f) max=(%.4f,%.4f)"),
				Group.GetValue(), *GroupNames[Group].ToString(), Tris.Num(), UVMin.X, UVMin.Y, UVMax.X, UVMax.Y);
		}
	}

	void InspectNiagaraModule(const FString& ScriptPath)
	{
		UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ScriptPath);
		const UNiagaraScriptSource* Source = Script ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
		const UNiagaraGraph* Graph = Source ? Source->NodeGraph.Get() : nullptr;
		if (!Graph)
		{
			UE_LOG(LogGlobeInspect, Error, TEXT("No graph for %s"), *ScriptPath);
			return;
		}
		UE_LOG(LogGlobeInspect, Display, TEXT("MODULE %s"), *ScriptPath);
		for (const auto& Pair : Graph->GetAllMetaData())
		{
			const FNiagaraVariable& Var = Pair.Key;
			if (Var.GetName().ToString().StartsWith(TEXT("Module.")))
			{
				const UNiagaraScriptVariable* SV = Pair.Value;
				FString Default;
				if (SV && SV->DefaultMode == ENiagaraDefaultMode::Binding)
				{
					Default = FString::Printf(TEXT("binding %s"), *SV->DefaultBinding.GetName().ToString());
				}
				else if (SV && SV->DefaultMode == ENiagaraDefaultMode::Custom)
				{
					Default = TEXT("custom (graph)");
				}
				else if (SV && Var.GetType() == FNiagaraTypeDefinition::GetVec3Def() && SV->GetDefaultValueData())
				{
					const FVector3f V = *reinterpret_cast<const FVector3f*>(SV->GetDefaultValueData());
					Default = FString::Printf(TEXT("value (%.2f,%.2f,%.2f)"), V.X, V.Y, V.Z);
				}
				else if (SV && (Var.GetType() == FNiagaraTypeDefinition::GetFloatDef()) && SV->GetDefaultValueData())
				{
					Default = FString::Printf(TEXT("value %.3f"), *reinterpret_cast<const float*>(SV->GetDefaultValueData()));
				}
				else if (SV && (Var.GetType() == FNiagaraTypeDefinition::GetBoolDef()) && SV->GetDefaultValueData())
				{
					Default = FString::Printf(TEXT("value %d"), *reinterpret_cast<const int32*>(SV->GetDefaultValueData()));
				}
				UE_LOG(LogGlobeInspect, Display, TEXT("   input %s : %s  [%s]"), *Var.GetName().ToString(), *Var.GetType().GetName(), *Default);
				if (const UEnum* Enum = Var.GetType().GetEnum())
				{
					FString Entries;
					for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
					{
						Entries += FString::Printf(TEXT("%lld=%s | "), Enum->GetValueByIndex(i), *Enum->GetDisplayNameTextByIndex(i).ToString());
					}
					UE_LOG(LogGlobeInspect, Display, TEXT("      enum %s: %s"), *Enum->GetName(), *Entries);
				}
			}
		}
		for (const FNiagaraVariable& Var : Graph->FindStaticSwitchInputs())
		{
			UE_LOG(LogGlobeInspect, Display, TEXT("   static %s : %s"), *Var.GetName().ToString(), *Var.GetType().GetName());
			if (const UEnum* Enum = Var.GetType().GetEnum())
			{
				FString Entries;
				for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
				{
					Entries += FString::Printf(TEXT("%lld=%s | "), Enum->GetValueByIndex(i), *Enum->GetDisplayNameTextByIndex(i).ToString());
				}
				UE_LOG(LogGlobeInspect, Display, TEXT("      enum %s: %s"), *Enum->GetName(), *Entries);
			}
		}
	}

	void InspectNiagaraEmitter(const FString& EmitterPath)
	{
		UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
		FVersionedNiagaraEmitterData* Data = Emitter ? Emitter->GetLatestEmitterData() : nullptr;
		const UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Source || !Source->NodeGraph)
		{
			UE_LOG(LogGlobeInspect, Error, TEXT("No emitter graph for %s"), *EmitterPath);
			return;
		}
		UE_LOG(LogGlobeInspect, Display, TEXT("EMITTER %s sim=%d local=%d"), *EmitterPath, (int32)Data->SimTarget, Data->bLocalSpace);
		for (const UEdGraphNode* Node : Source->NodeGraph->Nodes)
		{
			if (const UNiagaraNodeOutput* Out = Cast<UNiagaraNodeOutput>(Node))
			{
				UE_LOG(LogGlobeInspect, Display, TEXT("   output usage=%d"), (int32)Out->GetUsage());
			}
			else if (const UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				UE_LOG(LogGlobeInspect, Display, TEXT("   module '%s' script=%s"), *Call->GetFunctionName(), *GetPathNameSafe(Call->FunctionScript));
			}
		}
		for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			UE_LOG(LogGlobeInspect, Display, TEXT("   renderer %s"), *GetNameSafe(Renderer ? Renderer->GetClass() : nullptr));
		}
	}

	/** Prints every module's unlinked input pins (static switches live here) for each emitter of a system. */
	void InspectNiagaraSystem(const FString& SystemPath)
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
		if (!System)
		{
			UE_LOG(LogGlobeInspect, Error, TEXT("System not found: %s"), *SystemPath);
			return;
		}
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			const UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
			if (!Source || !Source->NodeGraph)
			{
				continue;
			}
			UE_LOG(LogGlobeInspect, Display, TEXT("SYSTEM %s emitter %s (unique name %s)"), *System->GetName(), *Handle.GetName().ToString(), *Handle.GetUniqueInstanceName());
			for (UNiagaraScript* Script : { Data->SpawnScriptProps.Script.Get(), Data->UpdateScriptProps.Script.Get() })
			{
				if (!Script)
				{
					continue;
				}
				TArray<FNiagaraVariable> RapidIteration;
				Script->RapidIterationParameters.GetParameters(RapidIteration);
				for (const FNiagaraVariable& Var : RapidIteration)
				{
					const FString Name = Var.GetName().ToString();
					if (Name.Contains(TEXT("ShapeLocation")) || Name.Contains(TEXT("Vortex")) || Name.Contains(TEXT("Index0Float")) || Name.Contains(TEXT("Index1Float")))
					{
						UE_LOG(LogGlobeInspect, Display, TEXT("   RI %s (%s)"), *Name, *Var.GetType().GetName());
					}
				}
			}
			for (const UEdGraphNode* Node : Source->NodeGraph->Nodes)
			{
				const UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
				if (!Call)
				{
					continue;
				}
				FString Pins;
				for (const UEdGraphPin* Pin : Call->Pins)
				{
					if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0 && !Pin->DefaultValue.IsEmpty())
					{
						Pins += FString::Printf(TEXT("%s=%s; "), *Pin->PinName.ToString(), *Pin->DefaultValue);
					}
				}
				UE_LOG(LogGlobeInspect, Display, TEXT("   %s: %s"), *Call->GetFunctionName(), *Pins);
			}
		}
	}

	void InspectActorPackage(const FString& PackageName)
	{
		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package)
		{
			UE_LOG(LogGlobeInspect, Error, TEXT("Could not load %s"), *PackageName);
			return;
		}
		ForEachObjectWithPackage(Package, [](UObject* Object)
		{
			const AActor* Actor = Cast<AActor>(Object);
			if (!Actor || Actor->IsTemplate())
			{
				return true;
			}
			const USceneComponent* Root = Actor->GetRootComponent();
			const FTransform T = Root ? Root->GetRelativeTransform() : FTransform::Identity;
			UE_LOG(LogGlobeInspect, Display, TEXT("ACTOR '%s' class=%s loc=%s rot=%s scale=%s"),
				*Actor->GetActorLabel(), *Actor->GetClass()->GetName(), *T.GetLocation().ToString(), *T.Rotator().ToString(), *T.GetScale3D().ToString());
			// Settings that differ from the class defaults (only for this project's classes).
			if (Actor->GetClass()->GetPathName().StartsWith(TEXT("/Script/VFX.")))
			{
				const UObject* Defaults = Actor->GetClass()->GetDefaultObject();
				for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
				{
					const FProperty* Property = *It;
					if (Property->HasAnyPropertyFlags(CPF_Transient) || Property->IsA<FObjectPropertyBase>() && CastField<FObjectPropertyBase>(Property)->PropertyClass->IsChildOf<UActorComponent>())
					{
						continue;
					}
					const void* Value = Property->ContainerPtrToValuePtr<void>(Actor);
					const void* DefaultValue = Property->ContainerPtrToValuePtr<void>(Defaults);
					if (!Property->Identical(Value, DefaultValue))
					{
						FString Text;
						Property->ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_None);
						UE_LOG(LogGlobeInspect, Display, TEXT("   set %s = %s"), *Property->GetName(), *Text.Left(200));
					}
				}
			}
			TArray<USceneComponent*> SceneComponents;
			Actor->GetComponents(SceneComponents);
			for (const USceneComponent* Component : SceneComponents)
			{
				if (Component != Root)
				{
					UE_LOG(LogGlobeInspect, Display, TEXT("   component '%s' (%s) relLoc=%s relRot=%s"), *Component->GetName(), *Component->GetClass()->GetName(),
						*Component->GetRelativeLocation().ToString(), *Component->GetRelativeRotation().ToString());
				}
			}
			TArray<UNiagaraComponent*> NiagaraComponents;
			Actor->GetComponents(NiagaraComponents);
			for (UNiagaraComponent* Niagara : NiagaraComponents)
			{
				TArray<FNiagaraVariable> Overrides;
				Niagara->GetOverrideParameters().GetParameters(Overrides);
				for (FNiagaraVariable& Var : Overrides)
				{
					FString Value;
					if (Var.GetType() == FNiagaraTypeDefinition::GetFloatDef())
					{
						Value = FString::SanitizeFloat(Niagara->GetOverrideParameters().GetParameterValue<float>(Var));
					}
					else if (Var.GetType() == FNiagaraTypeDefinition::GetColorDef())
					{
						Value = Niagara->GetOverrideParameters().GetParameterValue<FLinearColor>(Var).ToString();
					}
					UE_LOG(LogGlobeInspect, Display, TEXT("   niagara override %s = %s"), *Var.GetName().ToString(), *Value);
				}
			}
			if (const USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr)
			{
				UE_LOG(LogGlobeInspect, Display, TEXT("   attached to %s"), *GetNameSafe(Parent->GetOwner()));
			}

			TArray<UStaticMeshComponent*> Meshes;
			Actor->GetComponents(Meshes);
			for (const UStaticMeshComponent* SMC : Meshes)
			{
				UE_LOG(LogGlobeInspect, Display, TEXT("   mesh comp '%s' mesh=%s relLoc=%s relScale=%s mobility=%d"), *SMC->GetName(),
					*GetPathNameSafe(SMC->GetStaticMesh()), *SMC->GetRelativeLocation().ToString(), *SMC->GetRelativeScale3D().ToString(), (int32)SMC->Mobility.GetValue());
				for (int32 i = 0; i < SMC->GetNumOverrideMaterials(); ++i)
				{
					UE_LOG(LogGlobeInspect, Display, TEXT("     override %d = %s"), i, *GetPathNameSafe(SMC->OverrideMaterials[i]));
				}
			}
			return true;
		}, false);
	}
}

int32 UInspectGlobeSceneCommandlet::Main(const FString& Params)
{
	FString MeshPath;
	if (FParse::Value(*Params, TEXT("Mesh="), MeshPath))
	{
		InspectMesh(MeshPath);
	}

	FString ModuleList;
	if (FParse::Value(*Params, TEXT("NiagaraModules="), ModuleList))
	{
		TArray<FString> Modules;
		ModuleList.ParseIntoArray(Modules, TEXT("+"));
		for (const FString& Module : Modules)
		{
			InspectNiagaraModule(Module);
		}
	}

	FString SystemPath;
	if (FParse::Value(*Params, TEXT("NiagaraSystem="), SystemPath))
	{
		InspectNiagaraSystem(SystemPath);
	}

	FString EmitterPath;
	if (FParse::Value(*Params, TEXT("NiagaraEmitter="), EmitterPath))
	{
		InspectNiagaraEmitter(EmitterPath);
	}

	FString ActorList;
	if (FParse::Value(*Params, TEXT("Actors="), ActorList))
	{
		TArray<FString> Packages;
		ActorList.ParseIntoArray(Packages, TEXT("+"));
		for (const FString& Package : Packages)
		{
			InspectActorPackage(Package);
		}
	}
	return 0;
}
