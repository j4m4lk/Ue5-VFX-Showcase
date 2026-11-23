// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeVoiceVisualizerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Globe/GlobeVoiceInputComponent.h"
#include "Globe/GlobeVoiceVisualizer.h"

UGlobeVoiceVisualizer* UGlobeVoiceVisualizerWidget::BuildDefaultLayout(UWidgetTree* Tree)
{
	UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	Tree->RootWidget = Canvas;
	UGlobeVoiceVisualizer* NewVisualizer = Tree->ConstructWidget<UGlobeVoiceVisualizer>(UGlobeVoiceVisualizer::StaticClass(), TEXT("Visualizer"));
	UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(NewVisualizer);
	Slot->SetAnchors(FAnchors(0.5f, 1.0f));
	Slot->SetAlignment(FVector2D(0.5f, 1.0f));
	Slot->SetPosition(FVector2D(0.0f, -DefaultBottomMargin));
	Slot->SetAutoSize(true); // the visualizer sizes itself from its settings
	return NewVisualizer;
}

void UGlobeVoiceVisualizerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Visualizer = BuildDefaultLayout(WidgetTree); // plain C++ class, no Widget Blueprint
	}
	if (!Visualizer && WidgetTree)
	{
		WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			if (!Visualizer)
			{
				Visualizer = Cast<UGlobeVoiceVisualizer>(Widget); // renamed in the Widget Blueprint
			}
		});
	}
	SetVoiceInput(Voice.Get());
}

void UGlobeVoiceVisualizerWidget::SetVoiceInput(UGlobeVoiceInputComponent* InVoice)
{
	Voice = InVoice;
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([InVoice](UWidget* Widget)
		{
			if (UGlobeVoiceVisualizer* Each = Cast<UGlobeVoiceVisualizer>(Widget))
			{
				Each->SetVoiceInput(InVoice);
			}
		});
	}
}
