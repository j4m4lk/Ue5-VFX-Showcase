// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "InspectGlobeSceneCommandlet.generated.h"

/**
 * Read-only diagnostics for the globe setup: static mesh bounds / pivot / UV layout, material textures,
 * and placed actor transforms. Saves nothing.
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=InspectGlobeScene -Mesh=/Game/.../earth_globe -Actors=/Game/__ExternalActors__/...+...
 */
UCLASS()
class UInspectGlobeSceneCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UInspectGlobeSceneCommandlet();

	virtual int32 Main(const FString& Params) override;
};
