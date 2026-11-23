// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeShowcaseGameMode.h"

#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Globe/GlobeViewerPawn.h"

void AGlobeShowcaseGameMode::RestartPlayer(AController* NewPlayer)
{
	// A placed viewer pawn auto-possesses only after the GameMode would have spawned a DefaultPawn,
	// so hand it over here instead and skip the spawn entirely.
	if (NewPlayer && !NewPlayer->GetPawn())
	{
		for (TActorIterator<AGlobeViewerPawn> It(GetWorld()); It; ++It)
		{
			if (!It->GetController())
			{
				NewPlayer->Possess(*It);
				return;
			}
		}
	}

	Super::RestartPlayer(NewPlayer);
}
