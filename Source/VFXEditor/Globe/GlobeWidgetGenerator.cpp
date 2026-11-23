// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeWidgetGenerator.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Globe/GlobeAssetUtils.h"
#include "Globe/GlobeStatCardWidget.h"
#include "Globe/GlobeVFXParameters.h"
#include "Globe/GlobeVoiceVisualizer.h"
#include "Globe/GlobeVoiceVisualizerWidget.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"

namespace
{
	/**
	 * Creates /Game/AIGlobe/UI/<Name> (parent ParentClass) with the layout Build makes. An existing Widget Blueprint
	 * holds the user's edits: it is kept unless bOverwrite, which rebuilds its layout in place.
	 */
	bool GenerateWidgetBlueprint(const FString& Name, UClass* ParentClass, TFunctionRef<void(UWidgetTree*)> Build, bool bOverwrite)
	{
		const FString PackageName = FString(GlobeVFX::Assets::UIFolder) / Name;
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *Name);

		UWidgetBlueprint* WidgetBP = nullptr;
		if (UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			if (!bOverwrite)
			{
				UE_LOG(LogGlobeAssets, Display, TEXT("%s already exists (keeping your edits; pass -Overwrite / 'overwrite' to rebuild it)."), *ObjectPath);
				return true;
			}
			WidgetBP = Cast<UWidgetBlueprint>(Existing);
			if (!WidgetBP)
			{
				UE_LOG(LogGlobeAssets, Error, TEXT("%s exists but is a %s, not a Widget Blueprint."), *ObjectPath, *Existing->GetClass()->GetName());
				return false;
			}
			GlobeAssetUtils::CloseEditorsFor(WidgetBP);
			// Rebuild the layout in place (keeps the asset and every reference to it).
			TArray<UWidget*> Old;
			WidgetBP->WidgetTree->GetAllWidgets(Old);
			WidgetBP->WidgetTree->RootWidget = nullptr;
			for (UWidget* Widget : Old)
			{
				Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
			WidgetBP->WidgetVariableNameToGuidMap.Reset();
		}
		else
		{
			UPackage* Package = CreatePackage(*PackageName);
			Package->FullyLoad();
			WidgetBP = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal,
				UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
			if (!WidgetBP)
			{
				UE_LOG(LogGlobeAssets, Error, TEXT("Could not create %s."), *ObjectPath);
				return false;
			}
			FAssetRegistryModule::AssetCreated(WidgetBP);
		}

		Build(WidgetBP->WidgetTree);
		WidgetBP->WidgetTree->ForEachWidget([WidgetBP](UWidget* Widget)
		{
			Widget->SetFlags(RF_Transactional);
			Widget->bIsVariable = true; // named parts bind to the parent class's BindWidgetOptional members
			WidgetBP->OnVariableAdded(Widget->GetFName());
		});
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
		UE_LOG(LogGlobeAssets, Display, TEXT("Built %s."), *ObjectPath);
		return GlobeAssetUtils::SaveAsset(WidgetBP);
	}
}

bool FGlobeWidgetGenerator::GenerateVoiceVisualizerWidget(bool bOverwrite)
{
	return GenerateWidgetBlueprint(GlobeVFX::Assets::VoiceVisualizerWidgetName, UGlobeVoiceVisualizerWidget::StaticClass(),
		[](UWidgetTree* Tree) { UGlobeVoiceVisualizerWidget::BuildDefaultLayout(Tree); }, bOverwrite);
}

bool FGlobeWidgetGenerator::GenerateStatCardWidget(bool bOverwrite)
{
	return GenerateWidgetBlueprint(GlobeVFX::Assets::StatCardWidgetName, UGlobeStatCardWidget::StaticClass(),
		[](UWidgetTree* Tree) { UGlobeStatCardWidget::BuildDefaultLayout(Tree); }, bOverwrite);
}

bool FGlobeWidgetGenerator::GenerateAll(bool bOverwrite)
{
	const bool bVisualizer = GenerateVoiceVisualizerWidget(bOverwrite);
	const bool bCard = GenerateStatCardWidget(bOverwrite);
	return bVisualizer && bCard;
}
