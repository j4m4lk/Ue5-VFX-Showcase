// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

// Development-only helpers for checking the globe look without manual play-testing.

#if !UE_BUILD_SHIPPING

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Globe/GlobeVFXActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Globe/GlobeTimeOfDay.h"
#include "Globe/GlobeVoiceInputComponent.h"
#include "Globe/GlobeViewerPawn.h"
#include "Globe/GlobeVoiceVisualizer.h"
#include "Globe/GlobeVoiceVisualizerWidget.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Widgets/SWindow.h"

namespace
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

	AGlobeViewerPawn* FindViewer()
	{
		UWorld* World = FindGameWorld();
		return World ? *TActorIterator<AGlobeViewerPawn>(World) : nullptr;
	}

	/** bWithUI: include on-screen widgets (the voice visualizer); the scene tests leave them out. */
	void Shot(const FString& Name, bool bWithUI = false)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AIGlobe") / (Name + TEXT(".png"));
		// The viewport screenshot only holds the scene (+ world-space widgets); UMG viewport widgets are drawn later
		// into the Slate window, so a UI shot grabs the whole window instead.
		const TSharedPtr<SWindow> Window = (bWithUI && GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetWindow() : nullptr;
		TArray<FColor> Pixels;
		FIntVector PixelSize(0);
		if (Window.IsValid() && FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, PixelSize))
		{
			for (FColor& Pixel : Pixels)
			{
				Pixel.A = 255;
			}
			FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), PixelSize.X, PixelSize.Y));
		}
		else
		{
			FScreenshotRequest::RequestScreenshot(Path, bWithUI, false);
		}
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.CaptureSwapTest: %s"), *Path);
	}
}

namespace
{
	/** Ring mist: distance from the ring axis, height, and mean angular velocity (+ = same way as the core's spin). */
	void DumpRingMist()
	{
		for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
		{
			UNiagaraComponent* Component = It->GetCloudRingMist();
			FNiagaraSystemInstanceControllerPtr Controller = Component ? Component->GetSystemInstanceController() : nullptr;
			FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
			if (!Instance || Instance->GetEmitters().Num() == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpParticles: ring mist not running"));
				continue;
			}
			const FNiagaraDataSet& Data = Instance->GetEmitters()[0].Get().GetParticleData();
			const FNiagaraDataBuffer* Buffer = Data.GetCurrentData();
			const int32 Count = Buffer ? Buffer->GetNumInstances() : 0;
			auto Positions = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("Position"));
			auto Velocities = FNiagaraDataSetAccessor<FVector3f>::CreateReader(Data, TEXT("Velocity"));
			double SumR = 0.0, SumAbsZ = 0.0, SumOmega = 0.0;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector3f P = FVector3f(Positions.GetSafe(i, FNiagaraPosition(ForceInitToZero)));
				const FVector3f V = Velocities.GetSafe(i, FVector3f::ZeroVector);
				const float R2 = FMath::Max(1.0f, P.X * P.X + P.Y * P.Y);
				SumR += FMath::Sqrt(R2);
				SumAbsZ += FMath::Abs(P.Z);
				SumOmega += (P.X * V.Y - P.Y * V.X) / R2; // rad/s about local +Z (increasing angle)
			}
			const int32 N = FMath::Max(1, Count);
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpParticles: ring mist %d particles, radius from axis %.1f, |height| %.1f, spin %.2f deg/s"),
				Count, SumR / N, SumAbsZ / N, FMath::RadiansToDegrees(SumOmega / N));
		}
	}

	/**
	 * NS_GlobePrisms sparkles: count, radius range from the ring axis against the ribbon's inner/outer edge, height
	 * range against the wave height, and the closest distance to the globe centre against the globe radius.
	 */
	void DumpPrisms()
	{
		for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
		{
			UNiagaraComponent* Component = It->GetPrisms();
			FNiagaraSystemInstanceControllerPtr Controller = Component ? Component->GetSystemInstanceController() : nullptr;
			FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
			if (!Instance || Instance->GetEmitters().Num() == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpPrisms: sparkles not running"));
				continue;
			}
			auto UserFloat = [Component](const TCHAR* Name)
			{
				float Value = 0.0f;
				Component->GetOverrideParameters().GetParameterValue(Value, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), FName(FString(TEXT("User.")) + Name)));
				return Value;
			};
			const FNiagaraDataSet& Data = Instance->GetEmitters()[0].Get().GetParticleData();
			const FNiagaraDataBuffer* Buffer = Data.GetCurrentData();
			const int32 Count = Buffer ? Buffer->GetNumInstances() : 0;
			auto Positions = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("Position"));
			float MinRxy = FLT_MAX, MaxRxy = 0.0f, MaxZ = 0.0f, MinR = FLT_MAX;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector3f P = FVector3f(Positions.GetSafe(i, FNiagaraPosition(ForceInitToZero)));
				const float Rxy = FVector2f(P.X, P.Y).Size();
				MinRxy = FMath::Min(MinRxy, Rxy);
				MaxRxy = FMath::Max(MaxRxy, Rxy);
				MaxZ = FMath::Max(MaxZ, FMath::Abs(P.Z));
				MinR = FMath::Min(MinR, P.Size());
			}
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpPrisms: %d sparkles; ring radius %.0f..%.0f (ribbon %.0f..%.0f), |height| <= %.0f (wave %.0f), closest %.0f (globe %.0f), clock %.2f, ramp %.2f"),
				Count, Count ? MinRxy : 0.0f, MaxRxy, UserFloat(TEXT("RibbonInner")), UserFloat(TEXT("RibbonOuter")), MaxZ, UserFloat(TEXT("RibbonAmp")),
				Count ? MinR : 0.0f, UserFloat(TEXT("OccluderRadius")), UserFloat(TEXT("RibbonClock")), UserFloat(TEXT("PrismRamp")));
		}
	}

	/** Logs where NS_GlobeSurface's particles actually are, compared with the sphere they should sit on. */
	void DumpParticles()
	{
		DumpRingMist();
		DumpPrisms();
		for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
		{
			UNiagaraComponent* Component = It->GetSurfaceParticles();
			FNiagaraSystemInstanceControllerPtr Controller = Component ? Component->GetSystemInstanceController() : nullptr;
			FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
			if (!Instance || Instance->GetEmitters().Num() == 0)
			{
				UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpParticles: no running system instance"));
				continue;
			}
			float ExpectedRadius = 0.0f;
			float Variation = 0.0f;
			Component->GetOverrideParameters().GetParameterValue(ExpectedRadius, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.SurfaceRadius")));
			Component->GetOverrideParameters().GetParameterValue(Variation, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.SurfaceHeightVariation")));

			FNiagaraEmitterInstance& Emitter = Instance->GetEmitters()[0].Get();
			const FNiagaraDataSet& Data = Emitter.GetParticleData();
			const FNiagaraDataBuffer* Buffer = Data.GetCurrentData();
			const int32 Count = Buffer ? Buffer->GetNumInstances() : 0;
			auto Positions = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("Position"));
			auto Velocities = FNiagaraDataSetAccessor<FVector3f>::CreateReader(Data, TEXT("Velocity"));
			auto Ages = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("NormalizedAge"));

			// Buckets by normalised age: mean / min / max distance from the local origin, mean speed.
			constexpr int32 NumBuckets = 4;
			double Sum[NumBuckets] = {}, Speed[NumBuckets] = {};
			float Min[NumBuckets], Max[NumBuckets];
			int32 N[NumBuckets] = {};
			for (int32 b = 0; b < NumBuckets; ++b) { Min[b] = FLT_MAX; Max[b] = 0.0f; }
			FVector3f Centroid = FVector3f::ZeroVector;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector3f P = FVector3f(Positions.GetSafe(i, FNiagaraPosition(ForceInitToZero)));
				const float R = P.Size();
				const int32 b = FMath::Clamp(FMath::FloorToInt(Ages.GetSafe(i, 0.0f) * NumBuckets), 0, NumBuckets - 1);
				Sum[b] += R;
				Speed[b] += Velocities.GetSafe(i, FVector3f::ZeroVector).Size();
				Min[b] = FMath::Min(Min[b], R);
				Max[b] = FMath::Max(Max[b], R);
				++N[b];
				Centroid += P;
			}
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpParticles: %d particles, expected radius %.1f, centroid %s (local), component %s"),
				Count, ExpectedRadius, *(Count > 0 ? Centroid / Count : Centroid).ToString(), *Component->GetComponentLocation().ToString());
			for (int32 b = 0; b < NumBuckets; ++b)
			{
				if (N[b] > 0)
				{
					UE_LOG(LogTemp, Display, TEXT("AIGlobe.DumpParticles:   age %.2f-%.2f: n=%d radius mean %.1f min %.1f max %.1f, speed %.1f cm/s"),
						b / float(NumBuckets), (b + 1) / float(NumBuckets), N[b], Sum[b] / N[b], Min[b], Max[b], Speed[b] / N[b]);
				}
			}
		}
	}
}

namespace
{
	bool ParseTestAudio(const FString& Text, EGlobeTestAudio& Out)
	{
		const FString V = Text.TrimStartAndEnd().ToLower();
		if (V == TEXT("off")) { Out = EGlobeTestAudio::Off; return true; }
		if (V == TEXT("0")) { Out = EGlobeTestAudio::Level0; return true; }
		if (V == TEXT("0.25")) { Out = EGlobeTestAudio::Level25; return true; }
		if (V == TEXT("0.5")) { Out = EGlobeTestAudio::Level50; return true; }
		if (V == TEXT("1")) { Out = EGlobeTestAudio::Level100; return true; }
		if (V == TEXT("speech")) { Out = EGlobeTestAudio::SpeechPattern; return true; }
		return false;
	}

	void ApplyTestAudio(EGlobeTestAudio Mode)
	{
		for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
		{
			It->SetTestAudio(Mode);
		}
	}
}

/** AIGlobe.TestAudio 0|0.25|0.5|1|speech|off : drives the cloud ring's audio energy while playing. */
static FAutoConsoleCommand GTestAudio(
	TEXT("AIGlobe.TestAudio"),
	TEXT("Cloud ring test energy: 0 | 0.25 | 0.5 | 1 | speech | off"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		EGlobeTestAudio Mode;
		if (Args.Num() == 1 && ParseTestAudio(Args[0], Mode))
		{
			ApplyTestAudio(Mode);
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.TestAudio: %s"), *Args[0]);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: AIGlobe.TestAudio 0|0.25|0.5|1|speech|off"));
		}
	}));

namespace
{
	void ForEachGlobe(TFunctionRef<void(AGlobeVFXActor&)> Action)
	{
		for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
		{
			Action(**It);
		}
	}

	/** solo: only the prisms; all: every layer; on/off: the waves alone; energy=<0..1>. */
	void ApplyPrismsCommand(const FString& Arg)
	{
		float Energy = 0.0f;
		if (Arg == TEXT("solo")) { ForEachGlobe([](AGlobeVFXActor& G) { G.SetLayersEnabled(false, false, false, true); }); }
		else if (Arg == TEXT("all")) { ForEachGlobe([](AGlobeVFXActor& G) { G.SetLayersEnabled(true, true, true, true); }); }
		else if (Arg == TEXT("off")) { ForEachGlobe([](AGlobeVFXActor& G) { G.SetLayersEnabled(true, true, true, false); }); }
		else if (Arg == TEXT("on")) { ForEachGlobe([](AGlobeVFXActor& G) { G.SetLayersEnabled(true, true, true, true); }); }
		else if (FParse::Value(*Arg, TEXT("energy="), Energy)) { ForEachGlobe([Energy](AGlobeVFXActor& G) { G.SetEnergy(Energy); }); }
		else if (Arg.StartsWith(TEXT("lineglow=")) || Arg.StartsWith(TEXT("linewidth=")) || Arg.StartsWith(TEXT("linehalo=")))
		{
			// Trail lines: brightness, width (fraction of the band) and halo.
			ForEachGlobe([&Arg](AGlobeVFXActor& G)
			{
				for (const TPair<const TCHAR*, const TCHAR*>& Option : { TPair<const TCHAR*, const TCHAR*>(TEXT("lineglow="), TEXT("RibbonLineGlow")),
					TPair<const TCHAR*, const TCHAR*>(TEXT("linewidth="), TEXT("RibbonLineWidth")), TPair<const TCHAR*, const TCHAR*>(TEXT("linehalo="), TEXT("RibbonLineHalo")) })
				{
					float Value = 0.0f;
					if (FParse::Value(*Arg, Option.Key, Value))
					{
						FindFProperty<FFloatProperty>(AGlobeVFXActor::StaticClass(), Option.Value)->SetPropertyValue_InContainer(&G, FMath::Max(0.0f, Value));
					}
				}
				G.RefreshSurface();
			});
		}
		else if (Arg.StartsWith(TEXT("full=")) || Arg.StartsWith(TEXT("tilt=")) || Arg.StartsWith(TEXT("dir=")) || Arg.StartsWith(TEXT("size=")) || Arg.StartsWith(TEXT("intensity=")))
		{
			// Shape tweaks: start from the actor's current values and change only the one given.
			ForEachGlobe([&Arg](AGlobeVFXActor& G)
			{
				auto Get = [&G](const TCHAR* Name) { return FindFProperty<FFloatProperty>(AGlobeVFXActor::StaticClass(), Name)->GetPropertyValue_InContainer(&G); };
				bool bFull = FindFProperty<FBoolProperty>(AGlobeVFXActor::StaticClass(), TEXT("bPrismFullCircle"))->GetPropertyValue_InContainer(&G);
				float Tilt = Get(TEXT("PrismTilt")), Dir = Get(TEXT("PrismTiltDirection")), Size = Get(TEXT("PrismRadiusMultiplier")), Intensity = Get(TEXT("PrismIntensity"));
				int32 Full = bFull ? 1 : 0;
				FParse::Value(*Arg, TEXT("full="), Full);
				FParse::Value(*Arg, TEXT("tilt="), Tilt);
				FParse::Value(*Arg, TEXT("dir="), Dir);
				FParse::Value(*Arg, TEXT("size="), Size);
				FParse::Value(*Arg, TEXT("intensity="), Intensity);
				G.SetPrismShape(Full != 0, Tilt, Dir, Size, Intensity);
			});
		}
		else { UE_LOG(LogTemp, Warning, TEXT("Usage: AIGlobe.Prisms solo | all | on | off | energy=<0..1> | full=0|1 | tilt=<deg> | dir=<deg> | size=<0.6..3> | intensity=<x> | lineglow=<x> | linewidth=<0.001..0.3> | linehalo=<x>")); return; }
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.Prisms: %s"), *Arg);
	}
}

static FAutoConsoleCommand GPrisms(
	TEXT("AIGlobe.Prisms"),
	TEXT("Prisms: solo (prisms only) | all | on | off | energy=<0..1> | full=0|1 | tilt=<deg> | dir=<deg> | size=<x> | intensity=<x> | lineglow=<x> | linewidth=<x> | linehalo=<x> (several allowed)"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		for (const FString& Arg : Args)
		{
			ApplyPrismsCommand(Arg.ToLower());
		}
	}));

static FAutoConsoleCommand GDumpPrisms(
	TEXT("AIGlobe.DumpPrisms"),
	TEXT("Logs NS_GlobePrisms sparkles: count and where they sit relative to the ribbon and the globe."),
	FConsoleCommandDelegate::CreateStatic(&DumpPrisms));

/**
 * AIGlobe.CapturePrismsTest [solo] [quit]
 * Saved/AIGlobe/Prisms_*.png: energy 0 / 0.5 / 1 (two frames each, 0.5 s apart, to compare motion), then an S swap
 * (mid-crossfade and earth mode) and back to the hologram. Dumps ribbon diagnostics along the way.
 */
static FAutoConsoleCommand GCapturePrismsTest(
	TEXT("AIGlobe.CapturePrismsTest"),
	TEXT("Screenshots of the prisms at energy 0/0.5/1 and through an S swap into Saved/AIGlobe. Args: solo, quit"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bQuit = Args.Contains(TEXT("quit"));
		const bool bSolo = Args.Contains(TEXT("solo"));
		const FString Prefix = bSolo ? TEXT("Prisms_Solo_") : TEXT("Prisms_");
		struct FStep { float Delay; TFunction<void()> Action; };
		auto Toggle = [] { if (AGlobeViewerPawn* Viewer = FindViewer()) { Viewer->ToggleGlobe(); } };
		TArray<FStep> Steps = {
			{ 3.0f, [bSolo] { ApplyPrismsCommand(bSolo ? TEXT("solo") : TEXT("all")); ApplyPrismsCommand(TEXT("energy=0")); } },
			{ 3.0f, [Prefix] { Shot(Prefix + TEXT("E0_a")); } },
			{ 0.5f, [Prefix] { Shot(Prefix + TEXT("E0_b")); DumpPrisms(); } },
			{ 0.2f, [] { ApplyPrismsCommand(TEXT("energy=0.5")); } },
			{ 2.5f, [Prefix] { Shot(Prefix + TEXT("E05_a")); } },
			{ 0.5f, [Prefix] { Shot(Prefix + TEXT("E05_b")); } },
			{ 0.2f, [] { ApplyPrismsCommand(TEXT("energy=1")); } },
			{ 2.5f, [Prefix] { Shot(Prefix + TEXT("E1_a")); } },
			{ 0.5f, [Prefix] { Shot(Prefix + TEXT("E1_b")); DumpPrisms(); } },
			{ 0.2f, [Toggle] { ApplyPrismsCommand(TEXT("energy=0.5")); Toggle(); } },
			{ 0.6f, [Prefix] { Shot(Prefix + TEXT("Swap_Mid")); } },
			{ 2.5f, [Prefix] { Shot(Prefix + TEXT("Swap_Earth")); DumpPrisms(); } },
			// A/B for the far-side test: the same view with far-side trails left at full brightness.
			{ 0.1f, [] { ForEachGlobe([](AGlobeVFXActor& G) { FindFProperty<FFloatProperty>(AGlobeVFXActor::StaticClass(), TEXT("EarthModePrismBackSide"))->SetPropertyValue_InContainer(&G, 1.0f); G.RefreshSurface(); }); } },
			{ 0.1f, [Prefix] { Shot(Prefix + TEXT("Swap_Earth_NoBackSideHiding")); } },
			{ 0.1f, [] { ForEachGlobe([](AGlobeVFXActor& G) { FindFProperty<FFloatProperty>(AGlobeVFXActor::StaticClass(), TEXT("EarthModePrismBackSide"))->SetPropertyValue_InContainer(&G, 0.0f); G.RefreshSurface(); }); } },
			{ 0.2f, [Toggle] { Toggle(); } },
			{ 2.5f, [Prefix] { Shot(Prefix + TEXT("Swap_Back")); DumpPrisms(); } },
			// R key path (pawn TogglePrisms): off shrinks and fades, on grows from the min scale to full size.
			{ 0.2f, [] { if (AGlobeViewerPawn* Viewer = FindViewer()) { Viewer->TogglePrisms(); } } },
			{ 1.0f, [Prefix] { Shot(Prefix + TEXT("R_Off_Shrinking")); } },
			{ 2.5f, [Prefix] { Shot(Prefix + TEXT("R_Off")); DumpPrisms(); } },
			{ 0.2f, [] { if (AGlobeViewerPawn* Viewer = FindViewer()) { Viewer->TogglePrisms(); } } },
			{ 0.5f, [Prefix] { Shot(Prefix + TEXT("R_On_Growing_a")); } },
			{ 0.8f, [Prefix] { Shot(Prefix + TEXT("R_On_Growing_b")); } },
			{ 2.5f, [Prefix] { Shot(Prefix + TEXT("R_On_Full")); DumpPrisms(); } },
			{ 1.5f, [bQuit] { if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("AIGlobe.CapturePrismsTest")); } } },
		};
		double LastStep = FPlatformTime::Seconds();
		int32 Next = 0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, LastStep, Next](float) mutable
		{
#if WITH_EDITOR
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			{
				LastStep = FPlatformTime::Seconds(); // Don't capture half-compiled materials.
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

/**
 * AIGlobe.Rotation key=value ... : the Globe VFX|Rotation settings while playing, e.g.
 *   AIGlobe.Rotation lockglobe=1 globespin=10 ringlock=1 ringpitch=20 ringroll=0 ringyaw=0 ringspin=15
 *                    ribbonlock=1 ribbontilt=30 ribbondir=90 ribbonspin=5 reset
 */
static FAutoConsoleCommand GRotation(
	TEXT("AIGlobe.Rotation"),
	TEXT("Rotation: lockglobe= globespin= ringlock= ringpitch= ringroll= ringyaw= ringspin= ribbonlock= ribbontilt= ribbondir= ribbonspin= | reset"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		struct FOption { const TCHAR* Key; const TCHAR* Property; };
		static const FOption Floats[] = {
			{ TEXT("globespin="), TEXT("GlobeSpinSpeed") }, { TEXT("ringspin="), TEXT("RingSpinSpeed") },
			{ TEXT("ribbontilt="), TEXT("PrismTilt") }, { TEXT("ribbondir="), TEXT("PrismTiltDirection") }, { TEXT("ribbonspin="), TEXT("PrismSpinSpeed") } };
		static const FOption Bools[] = {
			{ TEXT("lockglobe="), TEXT("bLockGlobeRotation") }, { TEXT("ringlock="), TEXT("bLockVoiceRingRotation") }, { TEXT("ribbonlock="), TEXT("bLockPrismRotation") } };
		ForEachGlobe([&Args](AGlobeVFXActor& G)
		{
			UClass* Class = AGlobeVFXActor::StaticClass();
			FRotator& RingRotation = *FindFProperty<FStructProperty>(Class, TEXT("RingTilt"))->ContainerPtrToValuePtr<FRotator>(&G);
			for (const FString& RawArg : Args)
			{
				const FString Arg = RawArg.ToLower();
				float Value = 0.0f;
				for (const FOption& Option : Floats)
				{
					if (FParse::Value(*Arg, Option.Key, Value)) { FindFProperty<FFloatProperty>(Class, Option.Property)->SetPropertyValue_InContainer(&G, Value); }
				}
				for (const FOption& Option : Bools)
				{
					if (FParse::Value(*Arg, Option.Key, Value)) { FindFProperty<FBoolProperty>(Class, Option.Property)->SetPropertyValue_InContainer(&G, Value != 0.0f); }
				}
				if (FParse::Value(*Arg, TEXT("ringpitch="), Value)) { RingRotation.Pitch = Value; }
				if (FParse::Value(*Arg, TEXT("ringroll="), Value)) { RingRotation.Roll = Value; }
				if (FParse::Value(*Arg, TEXT("ringyaw="), Value)) { RingRotation.Yaw = Value; }
				if (Arg == TEXT("reset")) { G.ResetGlobeRotation(); }
				if (Arg == TEXT("turn")) { G.AddActorWorldRotation(FRotator(40.0f, 60.0f, 0.0f)); } // test: what a right-drag does
				if (Arg == TEXT("dump"))
				{
					auto Rot = [&G](const TCHAR* Name) -> FString
					{
						const FObjectProperty* Prop = FindFProperty<FObjectProperty>(AGlobeVFXActor::StaticClass(), Name);
						const USceneComponent* Component = Prop ? Cast<USceneComponent>(Prop->GetObjectPropertyValue_InContainer(&G)) : nullptr;
						return Component ? Component->GetComponentRotation().ToString() : TEXT("-");
					};
					UE_LOG(LogTemp, Display, TEXT("AIGlobe.Rotation dump: globe %s | voice ring %s | ribbon %s | ribbon sparkles %s | surface points %s"),
						*G.GetActorRotation().ToString(), *Rot(TEXT("CloudRingCore")), *Rot(TEXT("PrismRibbon")), *Rot(TEXT("Prisms")), *Rot(TEXT("SurfaceParticles")));
					continue; // no refresh needed for a dump
				}
			}
			G.RefreshSurface(); // re-places the ring and ribbons; nothing restarts
			if (UWorld* World = G.GetWorld(); World && World->IsGameWorld())
			{
				G.SetActorTickEnabled(true); // a new spin needs Tick (it switches itself off when idle)
			}
		});
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.Rotation: %s"), *FString::Join(Args, TEXT(" ")));
	}));

/** AIGlobe.Voice mic | sim | off | level | bars | waves : voice visualizer source, style, or log the microphone level / state. */
/** AIGlobe.Mist on | off : the cloud mist around the ring (the ring's core stays). */
static FAutoConsoleCommand GMist(
	TEXT("AIGlobe.Mist"),
	TEXT("Cloud mist around the ring: on | off (no argument toggles)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString Arg = Args.Num() > 0 ? Args[0].ToLower() : FString();
		ForEachGlobe([&Arg](AGlobeVFXActor& Globe)
		{
			Globe.SetCloudMistEnabled(Arg == TEXT("on") ? true : Arg == TEXT("off") ? false : !Globe.IsCloudMistEnabled());
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.Mist: %s"), Globe.IsCloudMistEnabled() ? TEXT("on") : TEXT("off"));
		});
	}));

/** AIGlobe.RingLine on | off : the ring's thin glowing line (the mist stays). */
static FAutoConsoleCommand GRingLine(
	TEXT("AIGlobe.RingLine"),
	TEXT("The ring's thin glowing line: on | off (no argument toggles)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString Arg = Args.Num() > 0 ? Args[0].ToLower() : FString();
		ForEachGlobe([&Arg](AGlobeVFXActor& Globe)
		{
			Globe.SetRingLineEnabled(Arg == TEXT("on") ? true : Arg == TEXT("off") ? false : !Globe.IsRingLineEnabled());
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.RingLine: %s"), Globe.IsRingLineEnabled() ? TEXT("on") : TEXT("off"));
		});
	}));

/** AIGlobe.Cloth on | off : cloth look on the prism ribbons (flutter, soft trail, faster). */
static FAutoConsoleCommand GCloth(
	TEXT("AIGlobe.Cloth"),
	TEXT("Cloth look on the prism ribbons: on | off (no argument toggles)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString Arg = Args.Num() > 0 ? Args[0].ToLower() : FString();
		ForEachGlobe([&Arg](AGlobeVFXActor& Globe)
		{
			Globe.SetClothRibbonsEnabled(Arg == TEXT("on") ? true : Arg == TEXT("off") ? false : !Globe.IsClothRibbonsEnabled());
			UE_LOG(LogTemp, Display, TEXT("AIGlobe.Cloth: %s"), Globe.IsClothRibbonsEnabled() ? TEXT("on") : TEXT("off"));
		});
	}));

static FAutoConsoleCommand GVoice(
	TEXT("AIGlobe.Voice"),
	TEXT("Voice visualizer: mic | sim (simulated speech) | off | level (log mic level, threshold and state) | bars | waves (style)"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		AGlobeViewerPawn* Viewer = FindViewer();
		UGlobeVoiceInputComponent* Voice = Viewer ? Viewer->FindComponentByClass<UGlobeVoiceInputComponent>() : nullptr;
		if (!Voice || Args.Num() != 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage (while playing): AIGlobe.Voice mic | sim | off | level | bars | waves"));
			return;
		}
		const FString Arg = Args[0].ToLower();
		if (Arg == TEXT("mic")) { Voice->SetSource(EGlobeVoiceSource::Microphone); }
		else if (Arg == TEXT("sim")) { Voice->SetSource(EGlobeVoiceSource::SimulatedSpeech); }
		else if (Arg == TEXT("off")) { Voice->SetSource(EGlobeVoiceSource::Off); }
		if (Arg == TEXT("bars") || Arg == TEXT("waves"))
		{
			UGlobeVoiceVisualizerWidget* Widget = Viewer->GetVoiceVisualizerWidget();
			if (UGlobeVoiceVisualizer* Visualizer = Widget ? Widget->GetVisualizer() : nullptr)
			{
				Visualizer->Style = Arg == TEXT("waves") ? EGlobeVoiceVisualizerStyle::Waves : EGlobeVoiceVisualizerStyle::Bars;
				Visualizer->SynchronizeProperties();
			}
		}
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.Voice: source %s, microphone %s, level %.1f dBFS (threshold %.1f), %s"),
			*StaticEnum<EGlobeVoiceSource>()->GetNameStringByValue((int64)Voice->Source), Voice->IsMicrophoneOpen() ? TEXT("open") : TEXT("closed"),
			Voice->GetLevelDb(), Voice->ThresholdDb, Voice->IsListening() ? TEXT("LISTENING") : TEXT("idle"));
	}));

/** AIGlobe.CaptureVoiceTest [quit] : Saved/AIGlobe/Voice_Idle.png (silent) and Voice_Listening.png (simulated speech). */
static FAutoConsoleCommand GCaptureVoiceTest(
	TEXT("AIGlobe.CaptureVoiceTest"),
	TEXT("Screenshots the voice visualizer idle and with simulated speech into Saved/AIGlobe. Optional arg: quit"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bQuit = Args.Contains(TEXT("quit"));
		auto SetSource = [](EGlobeVoiceSource Source)
		{
			AGlobeViewerPawn* Viewer = FindViewer();
			if (UGlobeVoiceInputComponent* Voice = Viewer ? Viewer->FindComponentByClass<UGlobeVoiceInputComponent>() : nullptr)
			{
				Voice->SetSource(Source);
			}
		};
		struct FStep { float Delay; TFunction<void()> Action; };
		TArray<FStep> Steps = {
			{ 3.0f, [SetSource] { SetSource(EGlobeVoiceSource::Off); } },
			{ 1.5f, [] { Shot(TEXT("Voice_Idle"), true); } },
			{ 0.1f, [SetSource] { SetSource(EGlobeVoiceSource::SimulatedSpeech); } },
			{ 1.1f, [] { Shot(TEXT("Voice_Listening"), true); } },
			{ 0.35f, [] { Shot(TEXT("Voice_Listening_b"), true); } },
			{ 1.0f, [bQuit] { if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("AIGlobe.CaptureVoiceTest")); } } },
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

/** AIGlobe.TimeOfDay <hours> | auto | stop : sets the level's time of day, or starts / stops the running day. */
static FAutoConsoleCommand GTimeOfDay(
	TEXT("AIGlobe.TimeOfDay"),
	TEXT("Time of day: <hours 0..24> | auto (let the day run) | stop"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		AGlobeTimeOfDay* TimeOfDay = AGlobeTimeOfDay::Find(FindGameWorld());
		if (!TimeOfDay || Args.Num() != 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage (while playing): AIGlobe.TimeOfDay <hours 0..24> | auto | stop"));
			return;
		}
		if (Args[0] == TEXT("auto")) { TimeOfDay->SetAutoAdvance(true); }
		else if (Args[0] == TEXT("stop")) { TimeOfDay->SetAutoAdvance(false); }
		else { TimeOfDay->SetTimeOfDay(FCString::Atof(*Args[0])); }
		UE_LOG(LogTemp, Display, TEXT("AIGlobe.TimeOfDay: %.2f h%s"), TimeOfDay->GetTimeOfDay(), TimeOfDay->IsAutoAdvancing() ? TEXT(" (running)") : TEXT(""));
	}));

/**
 * AIGlobe.CaptureCityLightsTest [quit] : earth mode, prisms off; Saved/AIGlobe/CityLights_<hour>.png through the day,
 * then CityLights_AllLand.png with the night-side rule off (shows where the land mask puts lights).
 */
static FAutoConsoleCommand GCaptureCityLightsTest(
	TEXT("AIGlobe.CaptureCityLightsTest"),
	TEXT("Screenshots the night-side city lights at several times of day into Saved/AIGlobe. Optional arg: quit"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bQuit = Args.Contains(TEXT("quit"));
		struct FStep { float Delay; TFunction<void()> Action; };
		TArray<FStep> Steps;
		Steps.Add({ 3.0f, [] { ApplyPrismsCommand(TEXT("off")); if (AGlobeViewerPawn* Viewer = FindViewer()) { Viewer->ToggleGlobe(); } } });
		for (const float Hour : { 12.0f, 17.5f, 19.0f, 22.0f, 2.0f })
		{
			Steps.Add({ 2.5f, [Hour] { if (AGlobeTimeOfDay* T = AGlobeTimeOfDay::Find(FindGameWorld())) { T->SetTimeOfDay(Hour); } } });
			Steps.Add({ 2.5f, [Hour] { Shot(FString::Printf(TEXT("CityLights_%04.1f"), Hour)); } });
		}
		Steps.Add({ 0.2f, [] { ForEachGlobe([](AGlobeVFXActor& G)
		{
			FindFProperty<FBoolProperty>(AGlobeVFXActor::StaticClass(), TEXT("bCityLightsNightSideOnly"))->SetPropertyValue_InContainer(&G, false);
			G.RefreshSurface();
		}); } });
		Steps.Add({ 1.5f, [] { Shot(TEXT("CityLights_AllLand")); } });
		// Night darkening A/B, a fraction of a second apart so auto exposure cannot adapt in between.
		auto SetDarkness = [](float Value) { ForEachGlobe([Value](AGlobeVFXActor& G) { FindFProperty<FFloatProperty>(AGlobeVFXActor::StaticClass(), TEXT("CityNightDarkness"))->SetPropertyValue_InContainer(&G, Value); G.RefreshSurface(); }); };
		Steps.Add({ 0.2f, [SetDarkness] { SetDarkness(0.0f); } });
		Steps.Add({ 0.25f, [] { Shot(TEXT("CityLights_Darkness0")); } });
		Steps.Add({ 0.05f, [SetDarkness] { SetDarkness(1.0f); } });
		Steps.Add({ 0.25f, [] { Shot(TEXT("CityLights_Darkness1")); } });
		Steps.Add({ 1.0f, [bQuit] { if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("AIGlobe.CaptureCityLightsTest")); } } });
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

/** AIGlobe.CaptureTimeOfDayTest [quit] : Saved/AIGlobe/TimeOfDay_<hour>.png at sunrise, noon, sunset, dusk and night. */
static FAutoConsoleCommand GCaptureTimeOfDayTest(
	TEXT("AIGlobe.CaptureTimeOfDayTest"),
	TEXT("Screenshots the level at several times of day into Saved/AIGlobe. Optional arg: quit"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bQuit = Args.Contains(TEXT("quit"));
		struct FStep { float Delay; TFunction<void()> Action; };
		TArray<FStep> Steps;
		Steps.Add({ 3.0f, [] {} });
		for (const float Hour : { 6.5f, 9.0f, 12.0f, 17.5f, 19.0f, 23.0f })
		{
			Steps.Add({ 0.2f, [Hour] { if (AGlobeTimeOfDay* T = AGlobeTimeOfDay::Find(FindGameWorld())) { T->SetTimeOfDay(Hour); } } });
			// Let auto exposure and the real-time sky capture settle before the shot.
			Steps.Add({ 3.0f, [Hour] { Shot(FString::Printf(TEXT("TimeOfDay_%04.1f"), Hour)); } });
		}
		Steps.Add({ 1.0f, [bQuit] { if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("AIGlobe.CaptureTimeOfDayTest")); } } });
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

static FAutoConsoleCommand GDumpParticles(
	TEXT("AIGlobe.DumpParticles"),
	TEXT("Logs NS_GlobeSurface particle distances from the globe centre vs. the expected radius."),
	FConsoleCommandDelegate::CreateStatic(&DumpParticles));

/**
 * AIGlobe.CaptureSwapTest [quit]
 * Screenshots the running game before, during and after an S swap (Saved/AIGlobe/Swap_*.png).
 * With "quit" the game exits when done (used for unattended checks).
 */
static FAutoConsoleCommand GCaptureSwapTest(
	TEXT("AIGlobe.CaptureSwapTest"),
	TEXT("Screenshots before/during/after a globe swap into Saved/AIGlobe. Optional arg: quit"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bQuit = Args.Contains(TEXT("quit"));
		// Optional "earthopacity=0.7": overrides the pawn's Earth Opacity for this capture.
		float EarthOpacity = -1.0f;
		for (const FString& Arg : Args)
		{
			FParse::Value(*Arg, TEXT("earthopacity="), EarthOpacity);
		}
		// Each step waits this long after the previous one (and until shaders finish compiling), one step per frame.
		struct FStep { float Delay; TFunction<void()> Action; };
		// Optional "frontonly": turns on GlobeVFXActor's Front Points Only for this capture.
		const bool bFrontOnly = Args.Contains(TEXT("frontonly"));
		const bool bRotateGlobe = Args.Contains(TEXT("rotateglobe"));
		// Optional "testaudio=1" (0|0.25|0.5|1|speech): cloud ring test energy for this capture.
		EGlobeTestAudio TestAudioMode = EGlobeTestAudio::Off;
		bool bSetTestAudio = false;
		for (const FString& Arg : Args)
		{
			FString Value;
			if (FParse::Value(*Arg, TEXT("testaudio="), Value))
			{
				bSetTestAudio = ParseTestAudio(Value, TestAudioMode);
			}
		}
		// Optional "holosurface": turns on Spawn On Hologram Surface for this capture.
		const bool bHoloSurface = Args.Contains(TEXT("holosurface"));
		TArray<FStep> Steps = {
			{ 3.0f, [bFrontOnly, bHoloSurface, bSetTestAudio, TestAudioMode]
			{
				if (bSetTestAudio)
				{
					ApplyTestAudio(TestAudioMode);
				}
				for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); It; ++It)
				{
					if (bFrontOnly)
					{
						FindFProperty<FBoolProperty>(AGlobeVFXActor::StaticClass(), TEXT("bFrontPointsOnly"))->SetPropertyValue_InContainer(*It, true);
					}
					if (bHoloSurface)
					{
						FindFProperty<FBoolProperty>(AGlobeVFXActor::StaticClass(), TEXT("bSpawnOnHologramSurface"))->SetPropertyValue_InContainer(*It, true);
					}
					It->RefreshSurface();
				}
			} },
			{ 1.0f, [] { Shot(TEXT("Swap_0_Hologram")); } },
			{ 0.2f, [] { DumpParticles(); } },
			{ 0.2f, [bRotateGlobe]
			{
				// Optional "rotateglobe": tilt/turn the globe rig to check what follows its rotation.
				for (TActorIterator<AGlobeVFXActor> It(FindGameWorld()); bRotateGlobe && It; ++It)
				{
					It->AddActorWorldRotation(FRotator(40.0f, 60.0f, 0.0f));
				}
			} },
			{ 1.0f, [bRotateGlobe] { if (bRotateGlobe) { Shot(TEXT("Swap_0b_Rotated")); } } },
			{ 1.0f, [EarthOpacity]
			{
				if (AGlobeViewerPawn* Viewer = FindViewer())
				{
					if (EarthOpacity >= 0.0f)
					{
						FindFProperty<FFloatProperty>(AGlobeViewerPawn::StaticClass(), TEXT("RealGlobeOpacity"))->SetPropertyValue_InContainer(Viewer, EarthOpacity);
					}
					Viewer->ToggleGlobe();
				}
			} },
			{ 0.6f, [] { Shot(TEXT("Swap_1_Mid")); } },
			{ 2.5f, [] { Shot(TEXT("Swap_2_Earth")); } },
			{ 5.0f, [] { Shot(TEXT("Swap_3_EarthLater")); } },
			{ 1.5f, [bQuit] { if (bQuit) { FPlatformMisc::RequestExit(false, TEXT("AIGlobe.CaptureSwapTest")); } } },
		};
		double LastStep = FPlatformTime::Seconds();
		int32 Next = 0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, LastStep, Next](float) mutable
		{
#if WITH_EDITOR
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			{
				LastStep = FPlatformTime::Seconds(); // Don't capture half-compiled materials.
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
