// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

// DEBUG ONLY (not in Shipping builds): sample items and checks for the four-link data display.
// The sample items are placeholders labelled "DEBUG"; they are not statistics. Production code supplies real
// items through UGlobeDataLinksComponent::SetDataItems / UpdateDataItem / AddDataItem / RemoveDataItem.

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Globe/GlobeVFXParameters.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Globe/GlobeStatCardWidget.h"
#include "UObject/UObjectIterator.h"
#include "Globe/GlobeDataLinksComponent.h"
#include "Globe/GlobeVFXActor.h"
#include "Globe/GlobeViewerPawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace GlobeDataDebug
{
	UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	TArray<UGlobeDataLinksComponent*> FindLinks()
	{
		TArray<UGlobeDataLinksComponent*> Result;
		for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
		{
			if (UGlobeDataLinksComponent* Links = It->GetDataLinks())
			{
				Result.Add(Links);
			}
		}
		return Result;
	}

	int32 UpdateCounter = 0;

	/** DEBUG placeholder item N (1-based). Values are a visible counter, not data. */
	FGlobeDataItem MakeSampleItem(int32 N, int32 Revision)
	{
		FGlobeDataItem Item;
		Item.Id = FName(*FString::Printf(TEXT("DebugItem%d"), N));
		Item.Title = FText::FromString(FString::Printf(TEXT("DEBUG SAMPLE %d"), N));
		Item.Value = FText::FromString(FString::Printf(TEXT("%d.%02d"), N * 100 + Revision, (Revision * 37) % 100));
		Item.Secondary = (N % 2 == 1) ? FText::FromString(FString::Printf(TEXT("placeholder rev %d"), Revision)) : FText::GetEmpty();
		Item.Subtitle = FText::FromString(FString::Printf(TEXT("Debug region %d"), N));
		for (int32 S = 1; S <= 1 + N; ++S)
		{
			Item.Stats.Add(FGlobeDataStat(FText::FromString(FString::Printf(TEXT("Stat %c"), TCHAR('A' + S - 1))),
				FText::FromString(FString::Printf(TEXT("%d"), N * 10 + S + Revision))));
		}
		Item.Details = FText::FromString(TEXT("DEBUG placeholder details text for the detail card."));
		Item.bHasProgress = N >= 3;
		Item.Progress = FMath::Frac(0.25f * N + 0.1f * Revision);
		return Item;
	}

	void SetSampleCount(int32 Count)
	{
		TArray<FGlobeDataItem> Items;
		for (int32 N = 1; N <= Count; ++N)
		{
			Items.Add(MakeSampleItem(N, UpdateCounter));
		}
		for (UGlobeDataLinksComponent* Links : FindLinks())
		{
			Links->SetDataItems(Items);
		}
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.TestDataItems: %d DEBUG sample items"), Count);
	}

	void UpdateSamples()
	{
		++UpdateCounter;
		for (UGlobeDataLinksComponent* Links : FindLinks())
		{
			for (const FGlobeDataItem& Existing : TArray<FGlobeDataItem>(Links->GetDataItems()))
			{
				const int32 N = FCString::Atoi(*Existing.Id.ToString().RightChop(9)); // "DebugItemN"
				FGlobeDataItem Updated = MakeSampleItem(N, UpdateCounter);
				Updated.Id = Existing.Id;
				Links->UpdateDataItem(Updated);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.TestDataUpdate: revision %d"), UpdateCounter);
	}

	/** Reads one emitter's sampled curve positions (component space) and normalized ages. */
	void DumpEmitter(UNiagaraComponent* Niagara, int32 EmitterIndex, const TCHAR* Label)
	{
		FNiagaraSystemInstanceControllerPtr Controller = Niagara ? Niagara->GetSystemInstanceController() : nullptr;
		FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
		if (!Instance || !Instance->GetEmitters().IsValidIndex(EmitterIndex))
		{
			UE_LOG(LogTemp, Display, TEXT("    %s: not running"), Label);
			return;
		}
		const FNiagaraDataSet& Data = Instance->GetEmitters()[EmitterIndex].Get().GetParticleData();
		const FNiagaraDataBuffer* Buffer = Data.GetCurrentData();
		const int32 Count = Buffer ? Buffer->GetNumInstances() : 0;
		auto SplineVec = FNiagaraDataSetAccessor<FVector3f>::CreateReader(Data, TEXT("SampleBezierSpline.SplinePosition"));
		auto SplinePos = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("SampleBezierSpline.SplinePosition"));
		auto Ages = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("NormalizedAge"));
		FVector3f Young(0), Old(0);
		float YoungAge = 2.0f, OldAge = -1.0f;
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector3f P = SplineVec.IsValid() ? SplineVec.GetSafe(i, FVector3f::ZeroVector) : FVector3f(SplinePos.GetSafe(i, FNiagaraPosition(ForceInitToZero)));
			const float Age = Ages.GetSafe(i, 0.0f);
			if (Age < YoungAge) { YoungAge = Age; Young = P; }
			if (Age > OldAge) { OldAge = Age; Old = P; }
		}
		UE_LOG(LogTemp, Display, TEXT("    %s: %d particles (spline attr %s); youngest age %.2f at %s; oldest age %.2f at %s"),
			Label, Count, SplineVec.IsValid() ? TEXT("vec3") : (SplinePos.IsValid() ? TEXT("position") : TEXT("MISSING")),
			YoungAge, *Young.ToString(), OldAge, *Old.ToString());
	}

	void DumpLinks()
	{
		for (UGlobeDataLinksComponent* Links : FindLinks())
		{
			const AGlobeVFXActor* Globe = Cast<AGlobeVFXActor>(Links->GetOwner());
			FVector2D Viewport(0);
			if (GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(Viewport); }
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpDataLinks: %d items, %d visible links, globe centre %s radius %.1f, viewport %.0fx%.0f"),
				Links->GetNumDataItems(), Links->GetNumVisibleLinks(), *Globe->GetGlobeCenter().ToString(), Globe->GetGlobeSurfaceRadius(), Viewport.X, Viewport.Y);
			for (const FGlobeDataItem& Item : Links->GetDataItems())
			{
				FVector World;
				FVector2D Screen;
				Links->GetLinkEndpoint(Item.Id, World, Screen);
				UE_LOG(LogTemp, Display, TEXT("  %s '%s' = '%s': endpoint local %s, screen (%.0f, %.0f)"), *Item.Id.ToString(), *Item.Title.ToString(), *Item.Value.ToString(),
					*(World - Globe->GetGlobeCenter()).ToString(), Screen.X, Screen.Y);
			}
			TArray<UNiagaraComponent*> Components;
			Links->GetOwner()->GetComponents<UNiagaraComponent>(Components);
			for (UNiagaraComponent* Niagara : Components)
			{
				if (!Niagara->GetName().StartsWith(TEXT("DataLink")) || !Niagara->IsActive())
				{
					continue;
				}
				FVector3f P0(0), P3(0);
				Niagara->GetOverrideParameters().GetParameterValue(P0, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.LinkP0")));
				Niagara->GetOverrideParameters().GetParameterValue(P3, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.LinkP3")));
				UE_LOG(LogTemp, Display, TEXT("  %s: anchor P0 %s, endpoint P3 %s"), *Niagara->GetName(), *P0.ToString(), *P3.ToString());
				DumpEmitter(Niagara, 0, TEXT("Stream"));
				DumpEmitter(Niagara, 1, TEXT("Filament"));
				DumpEmitter(Niagara, 2, TEXT("Node"));
			}
		}
	}

	void Shot(const FString& Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AIGlobe") / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, true, false); // with UI: the cards are UMG
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.CaptureDataLinksTest: %s"), *Path);
	}
}

/** DEBUG: AIGlobe.TestDataItems 0..4 (5+ tests the clamp). */
static FAutoConsoleCommand GTestDataItems(
	TEXT("AIGlobe.TestDataItems"),
	TEXT("DEBUG: shows N labelled placeholder data items (0-4; more tests the clamp)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		GlobeDataDebug::SetSampleCount(Args.Num() > 0 ? FMath::Max(0, FCString::Atoi(*Args[0])) : 4);
	}));

/** DEBUG: AIGlobe.TestDataUpdate - changes the shown items' values in place (streams keep running). */
static FAutoConsoleCommand GTestDataUpdate(
	TEXT("AIGlobe.TestDataUpdate"),
	TEXT("DEBUG: updates the values of the shown placeholder items."),
	FConsoleCommandDelegate::CreateStatic(&GlobeDataDebug::UpdateSamples));

/** DEBUG: AIGlobe.TestDataRemove N - removes placeholder item N; AIGlobe.TestDataAdd N - adds it back. */
static FAutoConsoleCommand GTestDataRemove(
	TEXT("AIGlobe.TestDataRemove"),
	TEXT("DEBUG: removes placeholder item N (1-4)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 N = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		for (UGlobeDataLinksComponent* Links : GlobeDataDebug::FindLinks())
		{
			Links->RemoveDataItem(FName(*FString::Printf(TEXT("DebugItem%d"), N)));
		}
	}));

static FAutoConsoleCommand GTestDataAdd(
	TEXT("AIGlobe.TestDataAdd"),
	TEXT("DEBUG: appends placeholder item N (1-9)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 N = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		for (UGlobeDataLinksComponent* Links : GlobeDataDebug::FindLinks())
		{
			Links->AddDataItem(GlobeDataDebug::MakeSampleItem(N, GlobeDataDebug::UpdateCounter));
		}
	}));

static FAutoConsoleCommand GDumpDataLinks(
	TEXT("AIGlobe.DumpDataLinks"),
	TEXT("Logs data items, endpoints and the particle positions along each link."),
	FConsoleCommandDelegate::CreateStatic(&GlobeDataDebug::DumpLinks));

/**
 * AIGlobe.CaptureDataLinksTest [quit]: screenshots (with UI) for 0-4 items, a value update, remove / add,
 * the S swap, globe movement / rotation / scale and a viewport resize into Saved/AIGlobe/Links_*.png.
 */
static FAutoConsoleCommand GCaptureDataLinksTest(
	TEXT("AIGlobe.CaptureDataLinksTest"),
	TEXT("DEBUG: screenshots the data-link display through its test cases. Optional arg: quit"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace GlobeDataDebug;
		const bool bQuit = Args.Contains(TEXT("quit"));
		struct FStep { float Delay; TFunction<void()> Action; };
		auto MoveGlobe = [](const FVector& Delta, const FRotator& Rotation, float ScaleFactor)
		{
			for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
			{
				It->AddActorWorldOffset(Delta);
				It->AddActorWorldRotation(Rotation);
				It->SetActorScale3D(It->GetActorScale3D() * ScaleFactor);
			}
		};
		auto Resize = [](int32 X, int32 Y)
		{
			if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid())
			{
				GEngine->GameViewport->GetWindow()->Resize(FVector2D(X, Y));
			}
		};
		TArray<FStep> Steps = {
			{ 3.0f, [] { SetSampleCount(0); } },
			{ 1.5f, [] { Shot(TEXT("Links_0")); } },
			{ 0.1f, [] { SetSampleCount(1); } },
			{ 2.5f, [] { Shot(TEXT("Links_1")); DumpLinks(); } },
			{ 0.1f, [] { SetSampleCount(2); } },
			{ 2.0f, [] { Shot(TEXT("Links_2")); } },
			{ 0.1f, [] { SetSampleCount(3); } },
			{ 2.0f, [] { Shot(TEXT("Links_3")); } },
			{ 0.1f, [] { SetSampleCount(4); } },
			{ 2.0f, [] { Shot(TEXT("Links_4")); DumpLinks(); } },
			{ 0.1f, [] { SetSampleCount(6); } }, // clamp: logs a warning, still 4
			{ 0.5f, [] { UpdateSamples(); } },
			{ 0.5f, [] { Shot(TEXT("Links_4_Updated")); DumpLinks(); } },
			{ 0.1f, [] { for (UGlobeDataLinksComponent* L : FindLinks()) { L->RemoveDataItem(TEXT("DebugItem1")); } } },
			{ 0.25f, [] { Shot(TEXT("Links_Remove_Mid")); } },
			{ 1.5f, [] { Shot(TEXT("Links_Remove_Done")); } },
			{ 0.1f, [] { for (UGlobeDataLinksComponent* L : FindLinks()) { L->AddDataItem(MakeSampleItem(1, UpdateCounter)); } } },
			{ 1.5f, [] { Shot(TEXT("Links_Readd")); } },
			{ 0.1f, [] { for (TActorIterator<AGlobeViewerPawn> It(FindGameWorld()); It; ++It) { It->ToggleGlobe(); } } },
			{ 0.6f, [] { Shot(TEXT("Links_Swap_Mid")); } },
			{ 2.5f, [] { Shot(TEXT("Links_Swap_Earth")); } },
			{ 0.1f, [MoveGlobe] { MoveGlobe(FVector(0.0, 600.0, 300.0), FRotator(20.0f, 45.0f, 0.0f), 0.8f); } },
			{ 1.5f, [] { Shot(TEXT("Links_Moved")); DumpLinks(); } },
			{ 0.1f, [MoveGlobe] { MoveGlobe(FVector(0.0, -600.0, -300.0), FRotator(-20.0f, -45.0f, 0.0f), 1.25f); } },
			{ 0.1f, [Resize] { Resize(1600, 600); } },
			{ 1.5f, [] { Shot(TEXT("Links_Wide")); DumpLinks(); } },
			{ 0.1f, [Resize] { Resize(720, 900); } },
			{ 1.5f, [] { Shot(TEXT("Links_Tall")); DumpLinks(); } },
			{ 0.1f, [Resize] { Resize(1280, 720); } },
			{ 0.1f, [] { SetSampleCount(0); } },
			{ 1.5f, [] { Shot(TEXT("Links_Back_To_0")); DumpLinks(); } },
			{ 1.0f, [bQuit] { if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("AIGlobe.CaptureDataLinksTest")); } } },
		};
		double LastStep = FPlatformTime::Seconds();
		int32 Next = 0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, LastStep, Next](float) mutable
		{
#if WITH_EDITOR
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			{
				LastStep = FPlatformTime::Seconds();
				return true;
			}
#endif
			if (Next < Steps.Num() && FPlatformTime::Seconds() - LastStep >= Steps[Next].Delay)
			{
				Steps[Next++].Action();
				LastStep = FPlatformTime::Seconds();
			}
			return Next < Steps.Num();
		}));
	}));


namespace GlobeDataDebug
{
	/** Clicks the centre of the shown card for Id through Slate (the real mouse routing, not a direct call). */
	bool ClickCard(FName Id)
	{
		for (TObjectIterator<UGlobeStatCardWidget> It; It; ++It)
		{
			if (It->GetItemId() != Id || !It->IsInViewport() || It->GetVisibility() != ESlateVisibility::Visible)
			{
				continue;
			}
			const FGeometry& Geometry = It->GetCachedGeometry();
			const FVector2D Center = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5;
			FSlateApplication& Slate = FSlateApplication::Get();
			Slate.SetCursorPos(Center);
			const TSet<FKey> Pressed = { EKeys::LeftMouseButton };
			FPointerEvent Down(Slate.CursorPointerIndex, Center, Center, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
			FPointerEvent Up(Slate.CursorPointerIndex, Center, Center, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
			Slate.ProcessMouseButtonDownEvent(nullptr, Down);
			Slate.ProcessMouseButtonUpEvent(Up);
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.CaptureDetailTest: clicked card '%s' at (%.0f, %.0f)"), *Id.ToString(), Center.X, Center.Y);
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("AIGlobe.CaptureDetailTest: no clickable card '%s'"), *Id.ToString());
		return false;
	}

	void LogSelection(const TCHAR* Step)
	{
		for (UGlobeDataLinksComponent* Links : FindLinks())
		{
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.CaptureDetailTest [%s]: %d items, %d visible links, selected '%s', detail mode %d"),
				Step, Links->GetNumDataItems(), Links->GetNumVisibleLinks(), *Links->GetSelectedItemId().ToString(), Links->IsDetailMode() ? 1 : 0);
		}
	}
}

/** DEBUG: AIGlobe.SelectDataItem Id - selects an item as clicking its card does. */
static FAutoConsoleCommand GSelectDataItem(
	TEXT("AIGlobe.SelectDataItem"),
	TEXT("DEBUG: selects the data item with this Id (none clears)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		for (UGlobeDataLinksComponent* Links : GlobeDataDebug::FindLinks())
		{
			Links->SelectDataItem(Args.Num() > 0 && Args[0] != TEXT("none") ? FName(*Args[0]) : NAME_None);
		}
	}));

/**
 * AIGlobe.CaptureDetailTest [quit]: adds two cards through the actor's Add Data Item function, clicks the second
 * card with a synthetic mouse click, presses S (one detail card), swaps back (both cards). Saved/AIGlobe/Detail_*.png
 */
static FAutoConsoleCommand GCaptureDetailTest(
	TEXT("AIGlobe.CaptureDetailTest"),
	TEXT("DEBUG: two cards via Add Data Item, click one, S swap to its detail card, swap back. Optional arg: quit"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace GlobeDataDebug;
		const bool bQuit = Args.Contains(TEXT("quit"));
		struct FStep { float Delay; TFunction<void()> Action; };
		auto Toggle = [] { for (TActorIterator<AGlobeViewerPawn> It(FindGameWorld()); It; ++It) { It->ToggleGlobe(); } };
		TArray<FStep> Steps = {
			{ 3.0f, []
			{
				for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
				{
					It->ClearAllDataItems();
					FTextProperty* NameProp = FindFProperty<FTextProperty>(AGlobeVFXActor::StaticClass(), TEXT("NewDataItemName"));
					for (const TCHAR* Name : { TEXT("DEBUG Alpha"), TEXT("DEBUG Beta") })
					{
						NameProp->SetPropertyValue_InContainer(*It, FText::FromString(Name));
						It->AddDataItemFromName();
					}
					// Placeholder detail text (DEBUG), then push the edited list as the Details panel would.
					FArrayProperty* ItemsProp = FindFProperty<FArrayProperty>(AGlobeVFXActor::StaticClass(), TEXT("DataItems"));
					TArray<FGlobeDataItem>& Items = *ItemsProp->ContainerPtrToValuePtr<TArray<FGlobeDataItem>>(*It);
					for (FGlobeDataItem& Item : Items)
					{
						Item.Details = FText::FromString(FString::Printf(TEXT("DEBUG placeholder details for %s.\nSecond line of detail text shown only on the detail card."), *Item.Title.ToString()));
					}
					It->ApplyDataItems();
				}
				LogSelection(TEXT("added"));
			} },
			{ 3.0f, [] { Shot(TEXT("Detail_0_TwoCards")); } },
			{ 0.2f, [] { ClickCard(TEXT("DEBUGBeta")); } },
			{ 0.5f, [] { LogSelection(TEXT("clicked")); Shot(TEXT("Detail_1_Selected")); } },
			{ 0.2f, [Toggle] { Toggle(); } },
			{ 3.0f, [] { LogSelection(TEXT("earth")); Shot(TEXT("Detail_2_EarthDetail")); } },
			{ 0.2f, [Toggle] { Toggle(); } },
			{ 3.0f, [] { LogSelection(TEXT("back")); Shot(TEXT("Detail_3_Back")); } },
			{ 1.0f, [bQuit] { if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("AIGlobe.CaptureDetailTest")); } } },
		};
		double LastStep = FPlatformTime::Seconds();
		int32 Next = 0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, LastStep, Next](float) mutable
		{
#if WITH_EDITOR
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			{
				LastStep = FPlatformTime::Seconds();
				return true;
			}
#endif
			if (Next < Steps.Num() && FPlatformTime::Seconds() - LastStep >= Steps[Next].Delay)
			{
				Steps[Next++].Action();
				LastStep = FPlatformTime::Seconds();
			}
			return Next < Steps.Num();
		}));
	}));
#endif

/** AIGlobe.DumpCardStyle : logs the saved look of WBP_GlobeStatCard (class settings + colours on its Designer widgets). */
static FAutoConsoleCommand GDumpCardStyle(
	TEXT("AIGlobe.DumpCardStyle"),
	TEXT("Logs WBP_GlobeStatCard's saved colours, blur and glass settings."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s_C"), GlobeVFX::Assets::UIFolder, GlobeVFX::Assets::StatCardWidgetName, GlobeVFX::Assets::StatCardWidgetName);
		UClass* Class = LoadClass<UGlobeStatCardWidget>(nullptr, *Path);
		if (!Class)
		{
			UE_LOG(LogTemp, Warning, TEXT("AIGlobe.DumpCardStyle: %s not found"), *Path);
			return;
		}
		// Every property of the Widget Blueprint's defaults that differs from the C++ defaults.
		const UObject* Defaults = Class->GetDefaultObject();
		const UObject* Native = UGlobeStatCardWidget::StaticClass()->GetDefaultObject();
		for (TFieldIterator<FProperty> It(UGlobeStatCardWidget::StaticClass()); It; ++It)
		{
			if (It->GetOwnerClass() == UGlobeStatCardWidget::StaticClass() && !It->Identical_InContainer(Defaults, Native))
			{
				FString Value;
				It->ExportTextItem_InContainer(Value, Defaults, nullptr, nullptr, PPF_None);
				UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpCardStyle: class default %s = %s"), *It->GetName(), *Value.Left(400));
			}
		}
		// Colours / blur set directly on the Designer widgets.
		if (const UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(Class))
		{
			if (const UWidgetTree* Tree = WidgetClass->GetWidgetTreeArchetype())
			{
				Tree->ForEachWidget([](UWidget* Widget)
				{
					const UObject* WidgetDefaults = Widget->GetClass()->GetDefaultObject();
					for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
					{
						const FString Name = It->GetName();
						if ((Name.Contains(TEXT("Color")) || Name.Contains(TEXT("Brush")) || Name.Contains(TEXT("Blur")) || Name.Contains(TEXT("Opacity")) || Name.Contains(TEXT("Corner")))
							&& !It->Identical_InContainer(Widget, WidgetDefaults))
						{
							FString Value;
							It->ExportTextItem_InContainer(Value, Widget, nullptr, nullptr, PPF_None);
							UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpCardStyle: widget %s.%s = %s"), *Widget->GetName(), *Name, *Value.Left(300));
						}
					}
				});
			}
		}
	}));
