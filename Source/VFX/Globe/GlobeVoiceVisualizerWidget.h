// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GlobeVoiceVisualizerWidget.generated.h"

class UGlobeVoiceInputComponent;
class UGlobeVoiceVisualizer;
class UWidgetTree;

/**
 * Full-screen HUD layer that holds the voice visualizer. Parent class of WBP_GlobeVoiceVisualizer
 * (Content/AIGlobe/UI): open that Widget Blueprint to move the visualizer and tweak it (select "Visualizer" in the
 * Hierarchy; its settings are in the Details panel). The viewer pawn adds it to the screen when play starts and
 * feeds it the microphone. Without a Widget Blueprint the same default layout is built in code.
 */
UCLASS()
class VFX_API UGlobeVoiceVisualizerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Feeds every Globe Voice Visualizer in this widget. */
	UFUNCTION(BlueprintCallable, Category = "Voice Visualizer")
	void SetVoiceInput(UGlobeVoiceInputComponent* InVoice);

	UFUNCTION(BlueprintPure, Category = "Voice Visualizer")
	UGlobeVoiceVisualizer* GetVisualizer() const { return Visualizer; }

	/** Default layout: canvas root with the visualizer docked at the bottom centre. Used by the asset generator too. */
	static UGlobeVoiceVisualizer* BuildDefaultLayout(UWidgetTree* Tree);

	static constexpr float DefaultBottomMargin = 40.0f;

protected:
	virtual void NativeOnInitialized() override;

	/** The visualizer in the layout (bound by name; optional). */
	UPROPERTY(BlueprintReadOnly, Category = "Voice Visualizer", meta = (BindWidgetOptional))
	TObjectPtr<UGlobeVoiceVisualizer> Visualizer;

private:
	TWeakObjectPtr<UGlobeVoiceInputComponent> Voice;
};
