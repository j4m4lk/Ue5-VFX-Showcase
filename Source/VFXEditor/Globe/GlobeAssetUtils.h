// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGlobeAssets, Log, All);

/** Shared idempotent asset helpers for the editor-only globe generators. */
namespace GlobeAssetUtils
{
	/** Saves the asset's package to disk. Returns false (and logs) on failure. */
	bool SaveAsset(UObject* Asset);

	/**
	 * Returns the existing asset of type T at Folder/Name, or creates a new one in a new package.
	 * Returns null (and logs) if a different asset type already occupies the path.
	 */
	template <typename T>
	T* FindOrCreateAsset(const FString& Folder, const FString& Name, bool& bOutCreated)
	{
		bOutCreated = false;
		const FString PackageName = Folder / Name;
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *Name);

		if (UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			T* Typed = Cast<T>(Existing);
			UE_CLOG(!Typed, LogGlobeAssets, Error, TEXT("%s exists but is a %s, not a %s. Rename or delete it and run again."),
				*ObjectPath, *Existing->GetClass()->GetName(), *T::StaticClass()->GetName());
			return Typed;
		}

		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		T* Asset = NewObject<T>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		bOutCreated = true;
		return Asset;
	}

	/** Closes any open asset editor for the object (their in-memory copies would fight a rebuild). */
	void CloseEditorsFor(UObject* Asset);
}
