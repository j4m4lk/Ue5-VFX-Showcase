// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GenerateGlobeMaterialsCommandlet.generated.h"

/**
 * Headless entry point for FGlobeMaterialGenerator:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=GenerateGlobeMaterials -unattended -nosplash [-AllowCommandletRendering]
 * With -AllowCommandletRendering it also waits for shader compilation and fails on material compile errors.
 */
UCLASS()
class UGenerateGlobeMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGenerateGlobeMaterialsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
