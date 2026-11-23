// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeAssetUtils.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

DEFINE_LOG_CATEGORY(LogGlobeAssets);

namespace GlobeAssetUtils
{
	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetPackage();
		Package->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
		UE_CLOG(!bSaved, LogGlobeAssets, Error, TEXT("Failed to save %s"), *Filename);
		UE_CLOG(bSaved, LogGlobeAssets, Display, TEXT("Saved %s"), *Package->GetName());
		return bSaved;
	}

	void CloseEditorsFor(UObject* Asset)
	{
		if (GEditor && Asset)
		{
			if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditors->CloseAllEditorsForAsset(Asset);
			}
		}
	}
}
