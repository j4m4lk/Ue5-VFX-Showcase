// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GlobeShowcaseGameMode.generated.h"

/**
 * GameMode for globe showcase levels. If the level contains a Globe Viewer Pawn, the player is given
 * that pawn directly, so no DefaultPawn (the floating sphere) is spawned at the PlayerStart.
 * Levels without one fall back to the normal GameModeBase behaviour.
 */
UCLASS(meta = (DisplayName = "Globe Showcase GameMode"))
class VFX_API AGlobeShowcaseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void RestartPlayer(AController* NewPlayer) override;
};
