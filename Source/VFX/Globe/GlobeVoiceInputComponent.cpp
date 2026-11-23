// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeVoiceInputComponent.h"

#include "AudioCaptureCore.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeVoice, Log, All);

/**
 * Default-microphone capture (Audio::FAudioCapture, 32-bit float stream) with a 1024-point FFT every 512 new
 * samples on the audio thread. Publishes per-band levels (dB, log-spaced 80 Hz .. 5 kHz) and the block RMS (dBFS).
 */
class FGlobeMicCapture
{
public:
	~FGlobeMicCapture() { Close(); }

	bool Open(int32 InNumBands)
	{
		NumBands = FMath::Max(2, InNumBands);
		Ring.Init(0.0f, FFTSize);
		LatestBandsDb.Init(-100.0f, NumBands);
		Audio::FAudioCaptureDeviceParams Params; // default device, its own rate / channels, float samples
		// OpenAudioCaptureStream is not exported from AudioCaptureCore in 5.6; the float-callback overload is.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const bool bOpened = Capture.OpenCaptureStream(Params,
			[this](const float* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate, double, bool)
			{
				OnAudio(InAudio, NumFrames, NumChannels, SampleRate);
			}, 512);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (!bOpened || !Capture.StartStream())
		{
			Capture.CloseStream();
			return false;
		}
		Audio::FCaptureDeviceInfo Info;
		if (Capture.GetCaptureDeviceInfo(Info))
		{
			UE_LOG(LogGlobeVoice, Display, TEXT("Microphone open: %s (%d Hz, %d channel(s))"), *Info.DeviceName, Info.PreferredSampleRate, Info.InputChannels);
		}
		return true;
	}

	void Close()
	{
		if (Capture.IsStreamOpen())
		{
			Capture.StopStream();
			Capture.CloseStream();
		}
	}

	bool IsOpen() const { return Capture.IsStreamOpen(); }

	/** Changes the number of bands (thread-safe; takes effect from the next analysis). */
	void SetNumBands(int32 InNumBands)
	{
		FScopeLock ScopeLock(&Lock);
		NumBands = FMath::Max(2, InNumBands);
		LatestBandsDb.Init(-100.0f, NumBands);
		bHasData = false;
	}

	/** Latest band levels (dB) and block level (dBFS); false until the first analysis. */
	bool Read(TArray<float>& OutBandsDb, float& OutLevelDb)
	{
		FScopeLock ScopeLock(&Lock);
		OutBandsDb = LatestBandsDb;
		OutLevelDb = LatestLevelDb;
		return bHasData;
	}

private:
	static constexpr int32 FFTSize = 1024;
	static constexpr int32 HopSize = 512;

	void OnAudio(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate)
	{
		if (!InAudio || NumChannels <= 0)
		{
			return;
		}
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			float Mono = 0.0f;
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				Mono += InAudio[Frame * NumChannels + Channel];
			}
			Ring[WritePos] = Mono / NumChannels;
			WritePos = (WritePos + 1) % FFTSize;
			if (++SinceAnalysis >= HopSize)
			{
				SinceAnalysis = 0;
				Analyse(SampleRate > 0 ? SampleRate : 48000);
			}
		}
	}

	/** In-place iterative radix-2 FFT. */
	static void FFT(TArray<float>& Re, TArray<float>& Im)
	{
		const int32 N = Re.Num();
		for (int32 i = 1, j = 0; i < N; ++i)
		{
			int32 Bit = N >> 1;
			for (; j & Bit; Bit >>= 1)
			{
				j ^= Bit;
			}
			j ^= Bit;
			if (i < j)
			{
				Swap(Re[i], Re[j]);
				Swap(Im[i], Im[j]);
			}
		}
		for (int32 Len = 2; Len <= N; Len <<= 1)
		{
			const float Angle = -UE_TWO_PI / Len;
			for (int32 i = 0; i < N; i += Len)
			{
				for (int32 k = 0; k < Len / 2; ++k)
				{
					const float C = FMath::Cos(Angle * k);
					const float S = FMath::Sin(Angle * k);
					const int32 A = i + k;
					const int32 B = A + Len / 2;
					const float TRe = Re[B] * C - Im[B] * S;
					const float TIm = Re[B] * S + Im[B] * C;
					Re[B] = Re[A] - TRe;
					Im[B] = Im[A] - TIm;
					Re[A] += TRe;
					Im[A] += TIm;
				}
			}
		}
	}

	void Analyse(int32 SampleRate)
	{
		Re.SetNumUninitialized(FFTSize);
		Im.Init(0.0f, FFTSize);
		double SumSq = 0.0;
		for (int32 i = 0; i < FFTSize; ++i)
		{
			const float Sample = Ring[(WritePos + i) % FFTSize]; // oldest first
			SumSq += Sample * Sample;
			Re[i] = Sample * (0.5f - 0.5f * FMath::Cos(UE_TWO_PI * i / (FFTSize - 1))); // Hann window
		}
		FFT(Re, Im);
		const float LevelDb = 10.0f * FMath::LogX(10.0f, (float)(SumSq / FFTSize) + 1e-12f);
		int32 Count;
		{
			FScopeLock ScopeLock(&Lock);
			Count = NumBands;
		}

		// Log-spaced voice bands; a sine of amplitude A peaks at A * N / 4 through a Hann window. Higher bands get a
		// +3 dB/octave tilt so speech fills the bars evenly instead of only the lowest ones.
		TArray<float> BandsDb;
		BandsDb.SetNumUninitialized(Count);
		const float BinHz = (float)SampleRate / FFTSize;
		for (int32 Band = 0; Band < Count; ++Band)
		{
			const float F0 = 80.0f * FMath::Pow(5000.0f / 80.0f, (float)Band / Count);
			const float F1 = 80.0f * FMath::Pow(5000.0f / 80.0f, (float)(Band + 1) / Count);
			const int32 B0 = FMath::Clamp(FMath::FloorToInt(F0 / BinHz), 1, FFTSize / 2 - 1);
			const int32 B1 = FMath::Clamp(FMath::CeilToInt(F1 / BinHz), B0 + 1, FFTSize / 2);
			float Peak = 0.0f;
			for (int32 Bin = B0; Bin < B1; ++Bin)
			{
				Peak = FMath::Max(Peak, FMath::Sqrt(Re[Bin] * Re[Bin] + Im[Bin] * Im[Bin]));
			}
			const float Amplitude = 4.0f * Peak / FFTSize;
			BandsDb[Band] = 20.0f * FMath::LogX(10.0f, Amplitude + 1e-9f) + 3.0f * FMath::Log2(FMath::Sqrt(F0 * F1) / 300.0f);
		}
		FScopeLock ScopeLock(&Lock);
		LatestBandsDb = MoveTemp(BandsDb);
		LatestLevelDb = LevelDb;
		bHasData = true;
	}

	Audio::FAudioCapture Capture;
	FCriticalSection Lock;
	TArray<float> Ring;
	TArray<float> Re;
	TArray<float> Im;
	TArray<float> LatestBandsDb;
	float LatestLevelDb = -100.0f;
	bool bHasData = false;
	int32 NumBands = 14;
	int32 WritePos = 0;
	int32 SinceAnalysis = 0;
};

UGlobeVoiceInputComponent::UGlobeVoiceInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UGlobeVoiceInputComponent::BeginPlay()
{
	Super::BeginPlay();
	Bands.Init(0.0f, NumBands);
	RawBands.Init(0.0f, NumBands);
	if (Source == EGlobeVoiceSource::Microphone)
	{
		OpenMicrophone();
	}
}

void UGlobeVoiceInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseMicrophone();
	Super::EndPlay(EndPlayReason);
}

void UGlobeVoiceInputComponent::SetSource(EGlobeVoiceSource NewSource)
{
	Source = NewSource;
	if (Source == EGlobeVoiceSource::Microphone)
	{
		OpenMicrophone();
	}
	else
	{
		CloseMicrophone();
	}
}

bool UGlobeVoiceInputComponent::IsMicrophoneOpen() const
{
	return Mic.IsValid() && Mic->IsOpen();
}

void UGlobeVoiceInputComponent::OpenMicrophone()
{
	if (IsMicrophoneOpen())
	{
		return;
	}
	Mic = MakeShared<FGlobeMicCapture>();
	if (!Mic->Open(NumBands))
	{
		UE_LOG(LogGlobeVoice, Warning, TEXT("%s: no microphone could be opened; the voice visualizer will idle."), *GetNameSafe(GetOwner()));
		Mic.Reset();
	}
}

void UGlobeVoiceInputComponent::CloseMicrophone()
{
	if (Mic.IsValid())
	{
		Mic->Close();
		Mic.Reset();
	}
}

void UGlobeVoiceInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Bands.Num() != NumBands)
	{
		Bands.Init(0.0f, NumBands);
		RawBands.Init(0.0f, NumBands);
	}

	// Target band levels 0..1 and the level in dBFS from the chosen source.
	float TargetDb = -100.0f;
	const float GainDb = 20.0f * FMath::LogX(10.0f, FMath::Max(Sensitivity, 0.01f));
	if (Source == EGlobeVoiceSource::Microphone && Mic.IsValid())
	{
		TArray<float> BandsDb;
		const bool bHasData = Mic->Read(BandsDb, TargetDb);
		if (BandsDb.Num() != NumBands)
		{
			Mic->SetNumBands(NumBands); // bar count changed after the microphone opened
		}
		else if (bHasData)
		{
			for (int32 i = 0; i < NumBands; ++i)
			{
				RawBands[i] = FMath::Clamp((BandsDb[i] + GainDb + 70.0f) / 45.0f, 0.0f, 1.0f); // -70 dB .. -25 dB
			}
		}
	}
	else if (Source == EGlobeVoiceSource::SimulatedSpeech)
	{
		// Phrases of ~3 s and ~1.2 s pauses, ~4.6 syllables per second; voice-like spectrum (strong low-mids).
		SimTime += DeltaTime;
		const float Phrase = FMath::Fmod(SimTime, 4.2f);
		const float Gate = FMath::SmoothStep(0.0f, 0.15f, Phrase) * (1.0f - FMath::SmoothStep(2.8f, 3.1f, Phrase));
		const float Syllable = FMath::Pow(FMath::Max(0.0f, FMath::Sin(UE_TWO_PI * 4.6f * SimTime)), 0.7f);
		const float Envelope = Gate * (0.25f + 0.75f * Syllable);
		for (int32 i = 0; i < NumBands; ++i)
		{
			const float X = (float)i / FMath::Max(1, NumBands - 1);
			const float Shape = FMath::Exp(-FMath::Square((X - 0.3f) / 0.35f));
			const float Flutter = 0.7f + 0.3f * FMath::Sin(SimTime * (7.0f + 3.0f * i) + i * 1.3f);
			RawBands[i] = FMath::Clamp(Envelope * Shape * Flutter * Sensitivity, 0.0f, 1.0f);
		}
		TargetDb = Envelope > 0.05f ? -30.0f + 10.0f * Envelope : -70.0f;
	}
	else
	{
		for (float& Value : RawBands)
		{
			Value = 0.0f;
		}
	}

	// Smooth: fast rise, slower fall (looks like a meter, never flickers).
	for (int32 i = 0; i < NumBands; ++i)
	{
		const float Speed = RawBands[i] > Bands[i] ? AttackSpeed : ReleaseSpeed;
		Bands[i] += (RawBands[i] - Bands[i]) * (1.0f - FMath::Exp(-Speed * DeltaTime));
	}
	LevelDb = TargetDb;
	const float TargetLevel = FMath::Clamp((TargetDb + GainDb + 60.0f) / 35.0f, 0.0f, 1.0f);
	Level += (TargetLevel - Level) * (1.0f - FMath::Exp(-(TargetLevel > Level ? AttackSpeed : ReleaseSpeed) * DeltaTime));

	// Talking = above the threshold; kept for HoldTime so short pauses between words do not flip the state.
	SinceVoice = TargetDb + GainDb > ThresholdDb ? 0.0f : SinceVoice + DeltaTime;
	const bool bNowListening = SinceVoice <= HoldTime;
	if (bNowListening != bListening)
	{
		bListening = bNowListening;
		OnListeningChanged.Broadcast(bListening);
	}
}
