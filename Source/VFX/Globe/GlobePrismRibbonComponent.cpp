// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobePrismRibbonComponent.h"

#include "Materials/MaterialInstanceDynamic.h"

UGlobePrismRibbonComponent::UGlobePrismRibbonComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	SetGenerateOverlapEvents(false);
	bUseAsyncCooking = false;
	bUseComplexAsSimpleCollision = false;
}

void UGlobePrismRibbonComponent::EnsureMaterial()
{
	if (!SheetMaterial)
	{
		return;
	}
	if (!SheetMID || SheetMID->Parent != SheetMaterial)
	{
		SheetMID = UMaterialInstanceDynamic::Create(SheetMaterial, this);
		SheetMID->SetFlags(RF_Transient);
	}
	if (GetNumSections() > 0 && GetMaterial(0) != SheetMID)
	{
		SetMaterial(0, SheetMID);
	}
}

void UGlobePrismRibbonComponent::SetScalar(FName Parameter, float Value)
{
	EnsureMaterial();
	if (SheetMID)
	{
		SheetMID->SetScalarParameterValue(Parameter, Value);
	}
}

void UGlobePrismRibbonComponent::SetVector(FName Parameter, const FLinearColor& Value)
{
	EnsureMaterial();
	if (SheetMID)
	{
		SheetMID->SetVectorParameterValue(Parameter, Value);
	}
}

void UGlobePrismRibbonComponent::SetWaveBounds(float HalfHeight)
{
	if (!FMath::IsNearlyEqual(WaveHalfHeight, HalfHeight, 1.0f))
	{
		WaveHalfHeight = HalfHeight;
		UpdateBounds();
		MarkRenderTransformDirty();
	}
}

FBoxSphereBounds UGlobePrismRibbonComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// The flat mesh has no height; the material lifts it by up to WaveHalfHeight along local Z.
	const float Outer = FMath::Max(BuiltOuter, 1.0f);
	const FBox LocalBox(FVector(-Outer, -Outer, -WaveHalfHeight), FVector(Outer, Outer, WaveHalfHeight));
	return FBoxSphereBounds(LocalBox).TransformBy(LocalToWorld);
}

void UGlobePrismRibbonComponent::Rebuild(float InnerRadius, float OuterRadius, int32 Layers, int32 Segments, int32 Echoes)
{
	EnsureMaterial();
	Segments = FMath::Clamp(Segments, 32, 1024);
	Layers = FMath::Clamp(Layers, 1, 8);
	Echoes = FMath::Clamp(Echoes, 0, 8);
	OuterRadius = FMath::Max(OuterRadius, InnerRadius + 1.0f);
	if (FMath::IsNearlyEqual(BuiltInner, InnerRadius, 0.01f) && FMath::IsNearlyEqual(BuiltOuter, OuterRadius, 0.01f)
		&& BuiltLayers == Layers && BuiltSegments == Segments && BuiltEchoes == Echoes)
	{
		return;
	}
	BuiltInner = InnerRadius;
	BuiltOuter = OuterRadius;
	BuiltLayers = Layers;
	BuiltSegments = Segments;
	BuiltEchoes = Echoes;

	// Rows across the band: enough for the material's twist across V to stay smooth.
	constexpr int32 Rows = 12;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	const int32 PerLayer = (Segments + 1) * (Rows + 1);
	Vertices.Reserve(PerLayer * Layers * (Echoes + 1));

	// Every sheet, then each echo of every sheet (same geometry; the material lags and fades the echoes).
	for (int32 Sheet = 0; Sheet < Layers * (Echoes + 1); ++Sheet)
	{
		const int32 Layer = Sheet % Layers;
		const int32 Echo = Sheet / Layers;
		const FLinearColor Color(Layers > 1 ? (float)Layer / (Layers - 1) : 0.0f, LayerSeed(Layer), (float)Echo / (Echoes + 1), 1.0f);
		const int32 Base = Vertices.Num();
		// Segments + 1 columns so the seam column (U = 1) sits on the first (U = 0): the waves use whole cycles.
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float U = (float)i / Segments;
			const float Angle = U * UE_TWO_PI;
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			const FVector Tangent(-Radial.Y, Radial.X, 0.0f);
			for (int32 Row = 0; Row <= Rows; ++Row)
			{
				const float V = (float)Row / Rows;
				Vertices.Add(Radial * FMath::Lerp(InnerRadius, OuterRadius, V));
				Normals.Add(FVector::UpVector);
				UVs.Emplace(U, V);
				Colors.Add(Color);
				Tangents.Emplace(Tangent, false);
			}
		}
		for (int32 i = 0; i < Segments; ++i)
		{
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const int32 A = Base + i * (Rows + 1) + Row;
				const int32 B = A + 1;
				const int32 C = A + (Rows + 1);
				const int32 D = C + 1;
				Triangles.Append({ A, C, B, B, C, D });
			}
		}
	}

	ClearAllMeshSections();
	CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	UpdateBounds();
	EnsureMaterial();
}
