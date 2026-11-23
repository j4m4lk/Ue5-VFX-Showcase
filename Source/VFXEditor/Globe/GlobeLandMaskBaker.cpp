// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeLandMaskBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Globe/GlobeAssetUtils.h"
#include "Globe/GlobeVFXParameters.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstance.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeLandMask, Log, All);

namespace
{
	UStaticMesh* FindMeshByName(const FString& Name)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.SearchAllAssets(true);
		TArray<FAssetData> Found;
		Registry.GetAssetsByClass(UStaticMesh::StaticClass()->GetClassPathName(), Found);
		for (const FAssetData& Asset : Found)
		{
			if (Asset.AssetName.ToString() == Name)
			{
				return Cast<UStaticMesh>(Asset.GetAsset());
			}
		}
		return nullptr;
	}

	/** Base colour texture of the material used by a mesh section (glTF/Interchange: "BaseColorTexture"). */
	UTexture2D* FindSectionTexture(const UStaticMesh* Mesh, FName SlotName)
	{
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			if (Slot.MaterialSlotName != SlotName && Slot.ImportedMaterialSlotName != SlotName)
			{
				continue;
			}
			UTexture* Texture = nullptr;
			if (Slot.MaterialInterface && Slot.MaterialInterface->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColorTexture")), Texture))
			{
				return Cast<UTexture2D>(Texture);
			}
		}
		return nullptr;
	}

	/**
	 * Land/ocean classification from a day-colour Earth texture. Oceans in this texture are saturated,
	 * blue-dominant and fairly dark; land is green/brown/tan and ice is bright and unsaturated.
	 */
	float ClassifyLand(const FColor& C)
	{
		const FLinearColor L = FLinearColor::FromSRGBColor(C);
		const float MaxC = FMath::Max3(L.R, L.G, L.B);
		const float MinC = FMath::Min3(L.R, L.G, L.B);
		const float Saturation = MaxC > 1e-4f ? (MaxC - MinC) / MaxC : 0.0f;
		const float BlueDominance = L.B - FMath::Max(L.R, L.G);

		// Bright and unsaturated = ice / cloud-free snow: count as land.
		if (MaxC > 0.55f && Saturation < 0.25f)
		{
			return 1.0f;
		}
		// Soft threshold on how much blue leads the other channels.
		const float OceanScore = FMath::SmoothStep(0.01f, 0.06f, BlueDominance) * FMath::SmoothStep(0.15f, 0.35f, Saturation);
		return 1.0f - OceanScore;
	}

	void BoxBlur(TArray<float>& Data, int32 W, int32 H, int32 Radius)
	{
		TArray<float> Temp;
		Temp.SetNumUninitialized(Data.Num());
		for (int32 y = 0; y < H; ++y) // horizontal, wrapping in longitude
		{
			for (int32 x = 0; x < W; ++x)
			{
				float Sum = 0.f;
				for (int32 k = -Radius; k <= Radius; ++k)
				{
					Sum += Data[((x + k + W) % W) + y * W];
				}
				Temp[x + y * W] = Sum / (2 * Radius + 1);
			}
		}
		for (int32 y = 0; y < H; ++y) // vertical, clamped at the poles
		{
			for (int32 x = 0; x < W; ++x)
			{
				float Sum = 0.f;
				for (int32 k = -Radius; k <= Radius; ++k)
				{
					Sum += Temp[x + FMath::Clamp(y + k, 0, H - 1) * W];
				}
				Data[x + y * W] = Sum / (2 * Radius + 1);
			}
		}
	}
}

/**
 * Projects every triangle of the earth mesh into longitude / latitude around its bounds centre and fills a W x H
 * equirectangular colour image from each triangle's own base colour texture (shared by the land and city masks).
 */
static bool BuildEquirect(const FString& MeshName, int32 Width, int32& OutW, int32& OutH, TArray<FColor>& OutEquirect)
{
	UStaticMesh* Mesh = FindMeshByName(MeshName);
	const FMeshDescription* Desc = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
	if (!Desc)
	{
		UE_LOG(LogGlobeLandMask, Error, TEXT("Earth mesh '%s' (or its LOD0 mesh description) not found."), *MeshName);
		return false;
	}

	const int32 W = FMath::Max(64, Width);
	const int32 H = W / 2;
	const FVector3f Center(Mesh->GetBounds().Origin);

	FStaticMeshConstAttributes Attributes(*Desc);
	const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
	const TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	const TPolygonGroupAttributesConstRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	TArray<FColor> Equirect;
	Equirect.Init(FColor(0, 0, 0, 0), W * H);

	for (const FPolygonGroupID Group : Desc->PolygonGroups().GetElementIDs())
	{
		UTexture2D* Texture = FindSectionTexture(Mesh, SlotNames[Group]);
		FImage Source;
		if (!Texture || !Texture->Source.IsValid() || !Texture->Source.GetMipImage(Source, 0))
		{
			UE_LOG(LogGlobeLandMask, Warning, TEXT("Section '%s' has no readable BaseColorTexture; skipped."), *SlotNames[Group].ToString());
			continue;
		}
		FImage Image;
		Source.CopyTo(Image, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		Source = FImage(); // free the source copy
		const TArrayView64<FColor> Pixels = Image.AsBGRA8();
		const int32 TW = Image.SizeX;
		const int32 TH = Image.SizeY;
		UE_LOG(LogGlobeLandMask, Display, TEXT("Section '%s': %s (%dx%d)"), *SlotNames[Group].ToString(), *Texture->GetName(), TW, TH);

		for (const FTriangleID Tri : Desc->GetPolygonGroupTriangles(Group))
		{
			const TArrayView<const FVertexInstanceID> Corners = Desc->GetTriangleVertexInstances(Tri);
			FVector2f P[3];
			FVector2f T[3];
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector3f Dir = (Positions[Desc->GetVertexInstanceVertex(Corners[i])] - Center).GetSafeNormal();
				const float Lon = FMath::Atan2(Dir.Y, Dir.X);
				const float Lat = FMath::Asin(FMath::Clamp(Dir.Z, -1.f, 1.f));
				P[i] = FVector2f((Lon / UE_TWO_PI + 0.5f) * W, (0.5f - Lat / UE_PI) * H);
				T[i] = UVs.Get(Corners[i], 0);
			}
			// Unwrap triangles that straddle the lon = +-180 seam.
			const float MinX = FMath::Min3(P[0].X, P[1].X, P[2].X);
			const float MaxX = FMath::Max3(P[0].X, P[1].X, P[2].X);
			if (MaxX - MinX > W * 0.5f)
			{
				for (FVector2f& Pt : P)
				{
					if (Pt.X < W * 0.5f)
					{
						Pt.X += W;
					}
				}
			}

			const float Area = (P[1].X - P[0].X) * (P[2].Y - P[0].Y) - (P[2].X - P[0].X) * (P[1].Y - P[0].Y);
			if (FMath::Abs(Area) < 1e-6f)
			{
				continue;
			}
			const int32 X0 = FMath::FloorToInt(FMath::Min3(P[0].X, P[1].X, P[2].X));
			const int32 X1 = FMath::CeilToInt(FMath::Max3(P[0].X, P[1].X, P[2].X));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(P[0].Y, P[1].Y, P[2].Y)));
			const int32 Y1 = FMath::Min(H - 1, FMath::CeilToInt(FMath::Max3(P[0].Y, P[1].Y, P[2].Y)));
			for (int32 y = Y0; y <= Y1; ++y)
			{
				for (int32 x = X0; x <= X1; ++x)
				{
					const FVector2f S(x + 0.5f, y + 0.5f);
					const float B0 = ((P[1].X - S.X) * (P[2].Y - S.Y) - (P[2].X - S.X) * (P[1].Y - S.Y)) / Area;
					const float B1 = ((P[2].X - S.X) * (P[0].Y - S.Y) - (P[0].X - S.X) * (P[2].Y - S.Y)) / Area;
					const float B2 = 1.0f - B0 - B1;
					if (B0 < -0.01f || B1 < -0.01f || B2 < -0.01f)
					{
						continue;
					}
					const FVector2f UV = T[0] * B0 + T[1] * B1 + T[2] * B2;
					const int32 TX = FMath::Clamp(FMath::FloorToInt(UV.X * TW), 0, TW - 1);
					const int32 TY = FMath::Clamp(FMath::FloorToInt(UV.Y * TH), 0, TH - 1);
					FColor Sample = Pixels[TX + (int64)TY * TW];
					Sample.A = 255;
					Equirect[((x % W) + W) % W + y * W] = Sample;
				}
			}
		}
	}

	// Fill the few unwritten pixels (pole fans, gaps) from written neighbours.
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		int32 Missing = 0;
		TArray<FColor> Next = Equirect;
		for (int32 y = 0; y < H; ++y)
		{
			for (int32 x = 0; x < W; ++x)
			{
				if (Equirect[x + y * W].A != 0)
				{
					continue;
				}
				int32 R = 0, G = 0, B = 0, N = 0;
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					for (int32 dx = -1; dx <= 1; ++dx)
					{
						const FColor& C = Equirect[((x + dx + W) % W) + FMath::Clamp(y + dy, 0, H - 1) * W];
						if (C.A != 0)
						{
							R += C.R; G += C.G; B += C.B; ++N;
						}
					}
				}
				if (N > 0)
				{
					Next[x + y * W] = FColor(R / N, G / N, B / N, 255);
				}
				else
				{
					++Missing;
				}
			}
		}
		Equirect = MoveTemp(Next);
		if (Missing == 0)
		{
			break;
		}
	}
	OutW = W;
	OutH = H;
	OutEquirect = MoveTemp(Equirect);
	return true;
}

/** Land (1) vs ocean (0) per pixel, with seabed speckle removed and coastlines slightly softened. */
static TArray<float> ComputeLand(const TArray<FColor>& Equirect, int32 W, int32 H)
{
	TArray<float> Land;
	Land.SetNumUninitialized(W * H);
	for (int32 i = 0; i < W * H; ++i)
	{
		Land[i] = ClassifyLand(Equirect[i]);
	}
	// Remove speckle: the ocean texture has thin pale seabed streaks that classify as land. A blur wide
	// enough to dissolve 1-2 px lines, then a re-threshold, keeps continents and drops the streaks.
	BoxBlur(Land, W, H, FMath::Max(2, W / 512));
	for (float& Value : Land)
	{
		Value = FMath::SmoothStep(0.4f, 0.6f, Value);
	}
	BoxBlur(Land, W, H, FMath::Max(1, W / 1024)); // soften coastlines slightly
	return Land;
}

/** Saves a G8 equirectangular mask as a texture asset (sampled at mip 0, wrapping in longitude). */
static bool SaveMaskTexture(const TCHAR* Name, const TArray<uint8>& Mask, int32 W, int32 H)
{
	bool bCreated = false;
	UTexture2D* MaskTexture = GlobeAssetUtils::FindOrCreateAsset<UTexture2D>(GlobeVFX::Assets::TextureFolder, Name, bCreated);
	if (!MaskTexture)
	{
		return false;
	}
	MaskTexture->Modify();
	MaskTexture->Source.Init(W, H, 1, 1, TSF_G8, Mask.GetData());
	MaskTexture->CompressionSettings = TC_Grayscale;
	MaskTexture->SRGB = false;
	MaskTexture->MipGenSettings = TMGS_NoMipmaps;
	MaskTexture->AddressX = TA_Wrap;
	MaskTexture->AddressY = TA_Clamp;
	MaskTexture->Filter = TF_Bilinear;
	MaskTexture->PostEditChange();
	UE_LOG(LogGlobeLandMask, Display, TEXT("%s %s (%dx%d)"), bCreated ? TEXT("Created") : TEXT("Updated"), *MaskTexture->GetPathName(), W, H);
	return GlobeAssetUtils::SaveAsset(MaskTexture);
}

bool FGlobeLandMaskBaker::Bake(const FString& MeshName, int32 Width)
{
	int32 W = 0, H = 0;
	TArray<FColor> Equirect;
	if (!BuildEquirect(MeshName, Width, W, H, Equirect))
	{
		return false;
	}

	TArray<float> Land = ComputeLand(Equirect, W, H);

	TArray<uint8> Mask;
	Mask.SetNumUninitialized(W * H);
	for (int32 i = 0; i < W * H; ++i)
	{
		Mask[i] = (uint8)FMath::Clamp(FMath::RoundToInt(Land[i] * 255.f), 0, 255);
	}

	// Previews for visual checking.
	const FString PreviewDir = FPaths::ProjectSavedDir() / TEXT("AIGlobe");
	FImageUtils::SaveImageByExtension(*(PreviewDir / TEXT("EarthEquirect.png")), FImageView(Equirect.GetData(), W, H));
	FImageUtils::SaveImageByExtension(*(PreviewDir / TEXT("LandMask.png")), FImageView(Mask.GetData(), W, H, ERawImageFormat::G8));

	UE_LOG(LogGlobeLandMask, Display, TEXT("Previews in %s"), *PreviewDir);
	return SaveMaskTexture(GlobeVFX::Assets::LandMaskName, Mask, W, H);
}

bool FGlobeLandMaskBaker::BakeCityMask(const FString& MeshName, int32 Width)
{
	int32 W = 0, H = 0;
	TArray<FColor> Equirect;
	if (!BuildEquirect(MeshName, Width, W, H, Equirect))
	{
		return false;
	}
	const TArray<float> Land = ComputeLand(Equirect, W, H);

	// Ice and snow: bright and unsaturated (the same rule that counts them as land in the land mask). Blurred and
	// re-thresholded so whole ice sheets (Antarctica, Greenland) go, not just their brightest pixels, and widened a
	// little so their coasts stay dark too.
	TArray<float> Ice;
	Ice.SetNumUninitialized(W * H);
	for (int32 i = 0; i < W * H; ++i)
	{
		const FLinearColor L = FLinearColor::FromSRGBColor(Equirect[i]);
		const float MaxC = FMath::Max3(L.R, L.G, L.B);
		const float MinC = FMath::Min3(L.R, L.G, L.B);
		const float Saturation = MaxC > 1e-4f ? (MaxC - MinC) / MaxC : 0.0f;
		Ice[i] = FMath::SmoothStep(0.45f, 0.6f, MaxC) * (1.0f - FMath::SmoothStep(0.18f, 0.3f, Saturation));
	}
	BoxBlur(Ice, W, H, FMath::Max(2, W / 256));
	for (float& Value : Ice)
	{
		Value = FMath::SmoothStep(0.25f, 0.5f, Value);
	}
	BoxBlur(Ice, W, H, FMath::Max(2, W / 512));

	TArray<uint8> Mask;
	Mask.SetNumUninitialized(W * H);
	for (int32 i = 0; i < W * H; ++i)
	{
		Mask[i] = (uint8)FMath::Clamp(FMath::RoundToInt(Land[i] * (1.0f - FMath::Clamp(Ice[i] * 1.5f, 0.0f, 1.0f)) * 255.f), 0, 255);
	}
	const FString PreviewDir = FPaths::ProjectSavedDir() / TEXT("AIGlobe");
	FImageUtils::SaveImageByExtension(*(PreviewDir / TEXT("CityMask.png")), FImageView(Mask.GetData(), W, H, ERawImageFormat::G8));
	return SaveMaskTexture(GlobeVFX::Assets::CityMaskName, Mask, W, H);
}
