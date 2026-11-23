// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeCloudRingComponent.h"

#include "Globe/GlobeVFXParameters.h"
#include "Materials/MaterialInstanceDynamic.h"

UGlobeCloudRingComponent::UGlobeCloudRingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	SetGenerateOverlapEvents(false);
	bUseAsyncCooking = false;
	bUseComplexAsSimpleCollision = false;
}

void UGlobeCloudRingComponent::EnsureMaterial()
{
	if (!CoreMaterial)
	{
		return;
	}
	if (!CoreMID || CoreMID->Parent != CoreMaterial)
	{
		CoreMID = UMaterialInstanceDynamic::Create(CoreMaterial, this);
		CoreMID->SetFlags(RF_Transient);
	}
	if (GetMaterial(0) != CoreMID)
	{
		SetMaterial(0, CoreMID);
	}
}

void UGlobeCloudRingComponent::SetLook(float Intensity, float Energy, float WaveHeight, const FLinearColor& Color)
{
	EnsureMaterial();
	if (CoreMID)
	{
		CoreMID->SetScalarParameterValue(GlobeVFX::Params::RingIntensity, Intensity);
		CoreMID->SetScalarParameterValue(GlobeVFX::Params::RingEnergy, Energy);
		CoreMID->SetScalarParameterValue(GlobeVFX::Params::WaveHeight, WaveHeight);
		CoreMID->SetVectorParameterValue(GlobeVFX::Params::RingColor, Color);
	}
}

void UGlobeCloudRingComponent::SetScalar(FName Parameter, float Value)
{
	EnsureMaterial();
	if (CoreMID)
	{
		CoreMID->SetScalarParameterValue(Parameter, Value);
	}
}

void UGlobeCloudRingComponent::Rebuild(float Radius, float Height, int32 Segments)
{
	EnsureMaterial();
	Segments = FMath::Clamp(Segments, 16, 1024);
	if (FMath::IsNearlyEqual(BuiltRadius, Radius, 0.01f) && FMath::IsNearlyEqual(BuiltHeight, Height, 0.01f) && BuiltSegments == Segments)
	{
		return;
	}
	BuiltRadius = Radius;
	BuiltHeight = Height;
	BuiltSegments = Segments;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	// Segments + 1 columns so the seam column (U = 1) sits on top of the first (U = 0): seamless waves.
	const float HalfHeight = Height * 0.5f;
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float U = (float)i / Segments;
		const float Angle = U * UE_TWO_PI;
		const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		const FVector Tangent(-Radial.Y, Radial.X, 0.0f);
		for (int32 Row = 0; Row < 2; ++Row)
		{
			Vertices.Add(Radial * Radius + FVector(0.0f, 0.0f, Row == 0 ? -HalfHeight : HalfHeight));
			Normals.Add(Radial);
			UVs.Emplace(U, Row == 0 ? 0.0f : 1.0f);
			Colors.Add(FLinearColor::White);
			Tangents.Emplace(Tangent, false);
		}
	}
	for (int32 i = 0; i < Segments; ++i)
	{
		const int32 A = i * 2, B = A + 1, C = A + 2, D = A + 3;
		Triangles.Append({ A, C, B, B, C, D });
	}

	ClearAllMeshSections();
	CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	// World Position Offset pushes the band outward with energy; widen bounds so it is never culled early.
	SetBoundsScale(1.3f);
	EnsureMaterial();
}
