// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeNetworkComponent.h"

#include "Globe/GlobeVFXParameters.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"

UGlobeNetworkComponent::UGlobeNetworkComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	SetGenerateOverlapEvents(false);
	bUseAsyncCooking = false;
	bUseComplexAsSimpleCollision = false;
}

int32 UGlobeNetworkComponent::EffectiveSeed()
{
	const UWorld* World = GetWorld();
	if (bRandomizeOnPlay && World && World->IsGameWorld())
	{
		if (RuntimeSeed == 0)
		{
			RuntimeSeed = FMath::RandRange(1, MAX_int32 - 1); // Once per play session; stable while playing.
		}
		return RuntimeSeed;
	}
	return Seed;
}

uint32 UGlobeNetworkComponent::SettingsHash(int32 LayoutSeed) const
{
	uint32 Hash = HashCombine(GetTypeHash(NodeCount), GetTypeHash(LayoutSeed));
	for (const float Value : { (float)LinksPerNode, (float)bUseCentimetreHeights, MinHeight, MaxHeight, MinAltitude, MaxAltitude, ArcLift, ArcLiftRandomness, RandomLinkChance, LinkWidth, NodeSize, (float)SegmentsPerLink })
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}
	return Hash;
}

void UGlobeNetworkComponent::PushGrowthParameters()
{
	if (NetworkMID)
	{
		// Growth runs in game only, so the editor shows the complete web for layout work.
		const UWorld* World = GetWorld();
		const bool bGrow = bAnimateGrowth && World && World->IsGameWorld();
		NetworkMID->SetScalarParameterValue(GlobeVFX::Params::GrowEnabled, bGrow ? 1.0f : 0.0f);
		NetworkMID->SetScalarParameterValue(GlobeVFX::Params::GrowCycle, GrowCycleSeconds);
	}
}

void UGlobeNetworkComponent::EnsureMaterial()
{
	if (!NetworkMaterial)
	{
		return;
	}
	if (!NetworkMID || NetworkMID->Parent != NetworkMaterial)
	{
		NetworkMID = UMaterialInstanceDynamic::Create(NetworkMaterial, this);
		NetworkMID->SetFlags(RF_Transient);
	}
	PushGrowthParameters();
	if (GetMaterial(0) != NetworkMID)
	{
		SetMaterial(0, NetworkMID);
	}
}

void UGlobeNetworkComponent::SetNetworkIntensity(float Intensity)
{
	EnsureMaterial();
	if (NetworkMID)
	{
		NetworkMID->SetScalarParameterValue(GlobeVFX::Params::NetworkIntensity, Intensity);
	}
}

void UGlobeNetworkComponent::SetCenterClear(float Amount, float Threshold, float Softness)
{
	EnsureMaterial();
	if (NetworkMID)
	{
		NetworkMID->SetScalarParameterValue(GlobeVFX::Params::CenterClearAmount, Amount);
		NetworkMID->SetScalarParameterValue(GlobeVFX::Params::CenterClearThreshold, Threshold);
		NetworkMID->SetScalarParameterValue(GlobeVFX::Params::CenterClearSoftness, Softness);
	}
}

void UGlobeNetworkComponent::Rebuild(float SurfaceRadius, float GlobeRadius)
{
	EnsureMaterial();
	const int32 LayoutSeed = EffectiveSeed();
	const uint32 Hash = SettingsHash(LayoutSeed);
	if (FMath::IsNearlyEqual(BuiltRadius, SurfaceRadius, 0.01f) && FMath::IsNearlyEqual(BuiltGlobeRadius, GlobeRadius, 0.01f) && Hash == BuiltSettingsHash)
	{
		return; // Geometry is unchanged; avoid re-uploading on every editor property tweak.
	}
	BuiltRadius = SurfaceRadius;
	BuiltGlobeRadius = GlobeRadius;
	BuiltSettingsHash = Hash;

	// --- Nodes: evenly spread directions (Fibonacci sphere) with seeded jitter and altitude. ---
	FRandomStream Random(LayoutSeed);
	const float SpiralOffset = Random.FRandRange(0.0f, UE_TWO_PI); // Rotates the whole layout per seed.
	TArray<FVector> Nodes;
	Nodes.Reserve(NodeCount);
	const float Golden = UE_PI * (3.0f - FMath::Sqrt(5.0f));
	for (int32 i = 0; i < NodeCount; ++i)
	{
		const float Z = 1.0f - 2.0f * (i + 0.5f) / NodeCount;
		const float R = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
		const float Theta = Golden * i + SpiralOffset;
		FVector Dir(FMath::Cos(Theta) * R, FMath::Sin(Theta) * R, Z);
		Dir = (Dir + Random.GetUnitVector() * 0.25f).GetSafeNormal();
		const float NodeRadius = bUseCentimetreHeights
			? GlobeRadius + Random.FRandRange(MinHeight, FMath::Max(MinHeight, MaxHeight))
			: SurfaceRadius * Random.FRandRange(MinAltitude, FMath::Max(MinAltitude, MaxAltitude));
		Nodes.Add(Dir * NodeRadius);
	}

	// --- Links: k nearest neighbours, de-duplicated (stable for a given seed). ---
	TSet<TPair<int32, int32>> Links;
	for (int32 a = 0; a < Nodes.Num(); ++a)
	{
		TArray<TPair<float, int32>> Neighbours;
		for (int32 b = 0; b < Nodes.Num(); ++b)
		{
			if (a != b)
			{
				Neighbours.Emplace(FVector::DistSquared(Nodes[a], Nodes[b]), b);
			}
		}
		Neighbours.Sort([](const TPair<float, int32>& L, const TPair<float, int32>& R) { return L.Key < R.Key; });
		for (int32 k = 0; k < FMath::Min(LinksPerNode, Neighbours.Num()); ++k)
		{
			const int32 b = Neighbours[k].Value;
			Links.Add(TPair<int32, int32>(FMath::Min(a, b), FMath::Max(a, b)));
		}
		// Occasional long-range link to a random node beyond the nearest few.
		if (Neighbours.Num() > LinksPerNode + 2 && Random.FRand() < RandomLinkChance)
		{
			const int32 b = Neighbours[Random.RandRange(LinksPerNode + 2, Neighbours.Num() - 1)].Value;
			Links.Add(TPair<int32, int32>(FMath::Min(a, b), FMath::Max(a, b)));
		}
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	auto AddQuad = [&](const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3,
		const FVector2D& UV0, const FVector2D& UV1, const FVector2D& UV2, const FVector2D& UV3, const FLinearColor& Color)
	{
		const int32 Base = Vertices.Num();
		const FVector Normal = FVector::CrossProduct(P1 - P0, P3 - P0).GetSafeNormal();
		for (const FVector& P : { P0, P1, P2, P3 })
		{
			Vertices.Add(P);
			Normals.Add(Normal);
			Colors.Add(Color);
			Tangents.Emplace((P1 - P0).GetSafeNormal(), false);
		}
		UVs.Append({ UV0, UV1, UV2, UV3 });
		Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	};

	// Links: an arc from node A to node B (slerped direction, lerped altitude plus a lift), drawn as two
	// crossed ribbons so it reads from any camera angle. Vertex colour A = 0 marks link geometry.
	const float HalfWidth = SurfaceRadius * LinkWidth * 0.5f;
	for (const TPair<int32, int32>& Link : Links)
	{
		const FVector A = Nodes[Link.Key];
		const FVector B = Nodes[Link.Value];
		const FQuat QA = FQuat::FindBetweenNormals(FVector::UpVector, A.GetSafeNormal());
		const FQuat QB = FQuat::FindBetweenNormals(FVector::UpVector, B.GetSafeNormal());
		// R = brightness, G = phase seed, B = growth-speed seed (see M_GlobeNetwork).
		const FLinearColor Color(Random.FRandRange(0.55f, 1.0f), Random.FRand(), Random.FRand(), 0.0f);
		const float Lift = ArcLift * (1.0f + Random.FRandRange(-ArcLiftRandomness, ArcLiftRandomness));

		TArray<FVector> Points;
		for (int32 s = 0; s <= SegmentsPerLink; ++s)
		{
			const float T = (float)s / SegmentsPerLink;
			const FVector Dir = FQuat::Slerp(QA, QB, T).RotateVector(FVector::UpVector);
			const float Radius = FMath::Lerp(A.Size(), B.Size(), T) + SurfaceRadius * Lift * FMath::Sin(T * UE_PI);
			Points.Add(Dir * Radius);
		}
		for (int32 s = 0; s < SegmentsPerLink; ++s)
		{
			const FVector P0 = Points[s];
			const FVector P1 = Points[s + 1];
			const FVector Tangent = (P1 - P0).GetSafeNormal();
			const FVector Radial = ((P0 + P1) * 0.5f).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Tangent, Radial).GetSafeNormal();
			const float U0 = (float)s / SegmentsPerLink;
			const float U1 = (float)(s + 1) / SegmentsPerLink;
			for (const FVector& Axis : { Side, Radial })
			{
				AddQuad(P0 - Axis * HalfWidth, P1 - Axis * HalfWidth, P1 + Axis * HalfWidth, P0 + Axis * HalfWidth,
					FVector2D(U0, 0), FVector2D(U1, 0), FVector2D(U1, 1), FVector2D(U0, 1), Color);
			}
		}
	}

	// Nodes: three orthogonal soft-dot quads (a "star" that reads from any angle). Vertex colour A = 1.
	const float HalfNode = SurfaceRadius * NodeSize * 0.5f;
	for (const FVector& Node : Nodes)
	{
		const FLinearColor Color(Random.FRandRange(0.7f, 1.0f), 0.0f, 0.0f, 1.0f);
		const FVector Axes[3][2] = { { FVector::ForwardVector, FVector::RightVector }, { FVector::RightVector, FVector::UpVector }, { FVector::UpVector, FVector::ForwardVector } };
		for (const auto& Pair : Axes)
		{
			const FVector X = Pair[0] * HalfNode;
			const FVector Y = Pair[1] * HalfNode;
			AddQuad(Node - X - Y, Node + X - Y, Node + X + Y, Node - X + Y,
				FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1), Color);
		}
	}

	ClearAllMeshSections();
	CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	EnsureMaterial();
}
