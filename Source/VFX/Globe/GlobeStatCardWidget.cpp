// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeStatCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Globe/GlobeCardEdgeFlow.h"
#include "Globe/GlobeStatIcon.h"
#include "Input/Reply.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "GlobeStatCard"

namespace
{
	template <typename T>
	T* Make(UWidgetTree* Tree, const TCHAR* Name)
	{
		return Tree->ConstructWidget<T>(T::StaticClass(), FName(Name));
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A = Alpha;
		return Color;
	}

	FSlateFontInfo Font(const TCHAR* Typeface, int32 Size, int32 LetterSpacing = 0)
	{
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Typeface, Size);
		Info.LetterSpacing = LetterSpacing;
		return Info;
	}

	void SetShown(UWidget* Widget, bool bShown)
	{
		if (Widget)
		{
			Widget->SetVisibility(bShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	/**
	 * Flag colours for common countries, without white (the link's core stream is white), used when a country
	 * entry has no Colors of its own. Keys are lower case; a few common alternative names are included.
	 */
	TArray<FLinearColor> BuiltInFlagColors(const FString& CountryName)
	{
		const FLinearColor Red(0.95f, 0.06f, 0.08f), Green(0.0f, 0.62f, 0.25f), DarkGreen(0.0f, 0.42f, 0.2f), Blue(0.05f, 0.3f, 1.0f),
			Navy(0.1f, 0.2f, 0.75f), Gold(1.0f, 0.75f, 0.1f), Orange(1.0f, 0.5f, 0.1f), Maroon(0.55f, 0.05f, 0.2f), Sky(0.3f, 0.7f, 1.0f);
		static const TMap<FString, TArray<FLinearColor>> Palette = {
			{ TEXT("lebanon"), { Red, Green } },
			{ TEXT("uae"), { Green, Red } },
			{ TEXT("united arab emirates"), { Green, Red } },
			{ TEXT("pakistan"), { DarkGreen } },
			{ TEXT("saudi arabia"), { Green } },
			{ TEXT("ksa"), { Green } },
			{ TEXT("qatar"), { Maroon } },
			{ TEXT("kuwait"), { Green, Red } },
			{ TEXT("oman"), { Red, Green } },
			{ TEXT("bahrain"), { Red } },
			{ TEXT("jordan"), { Red, Green } },
			{ TEXT("egypt"), { Red, Gold } },
			{ TEXT("iraq"), { Red, Green } },
			{ TEXT("turkey"), { Red } },
			{ TEXT("iran"), { Green, Red } },
			{ TEXT("india"), { Orange, Green, Navy } },
			{ TEXT("bangladesh"), { DarkGreen, Red } },
			{ TEXT("sri lanka"), { Maroon, Gold, Green } },
			{ TEXT("nepal"), { Red, Navy } },
			{ TEXT("philippines"), { Blue, Red, Gold } },
			{ TEXT("uk"), { Navy, Red } },
			{ TEXT("united kingdom"), { Navy, Red } },
			{ TEXT("usa"), { Navy, Red } },
			{ TEXT("united states"), { Navy, Red } },
			{ TEXT("france"), { Navy, Red } },
			{ TEXT("germany"), { Red, Gold } },
			{ TEXT("italy"), { Green, Red } },
			{ TEXT("spain"), { Red, Gold } },
			{ TEXT("canada"), { Red } },
			{ TEXT("china"), { Red, Gold } },
			{ TEXT("japan"), { Red } },
			{ TEXT("nigeria"), { Green } },
			{ TEXT("south africa"), { Green, Gold, Red, Navy } },
			{ TEXT("argentina"), { Sky, Gold } },
		};
		const TArray<FLinearColor>* Found = Palette.Find(CountryName.TrimStartAndEnd().ToLower());
		return Found ? *Found : TArray<FLinearColor>();
	}

	FGlobeCardCountryData MakeCountry(const TCHAR* Name, const FText& Subtitle, int64 Applicants, int64 Centers, int64 Approved, int64 Biometrics,
		std::initializer_list<FLinearColor> Colors)
	{
		FGlobeCardCountryData Country;
		Country.Name = FText::FromString(Name);
		Country.Subtitle = Subtitle;
		Country.Values.Add(TEXT("Applicants"), Applicants);
		Country.Values.Add(TEXT("Centers"), Centers);
		Country.Values.Add(TEXT("Approved"), Approved);
		Country.Values.Add(TEXT("Biometrics"), Biometrics);
		Country.Colors = Colors;
		Country.bSampleData = true;
		return Country;
	}
}

UGlobeStatCardWidget::UGlobeStatCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Visa activity categories (icons drawn by UGlobeStatIcon).
	Categories = {
		FGlobeCardStatCategory(TEXT("Applicants"), LOCTEXT("CatApplicants", "Applicants"), EGlobeStatIcon::People),
		FGlobeCardStatCategory(TEXT("Centers"), LOCTEXT("CatCenters", "Active centers"), EGlobeStatIcon::Building),
		FGlobeCardStatCategory(TEXT("Approved"), LOCTEXT("CatApproved", "Visas approved"), EGlobeStatIcon::CheckBadge),
		FGlobeCardStatCategory(TEXT("Biometrics"), LOCTEXT("CatBiometrics", "Biometrics done"), EGlobeStatIcon::Fingerprint),
	};
	// PLACEHOLDER numbers (Sample Data ticked, so the card shows the Sample Badge): replace them with the real figures.
	Countries = {
		// Flag colours (the link's white core is the white).
		MakeCountry(TEXT("Lebanon"), LOCTEXT("SubLebanon", "Visa operations"), 12480, 3, 9860, 11230,
			{ FLinearColor(0.95f, 0.06f, 0.08f, 1.0f), FLinearColor(0.0f, 0.62f, 0.25f, 1.0f) }),
		MakeCountry(TEXT("UAE"), LOCTEXT("SubUAE", "Visa operations"), 48250, 7, 39400, 45120,
			{ FLinearColor(0.0f, 0.6f, 0.25f, 1.0f), FLinearColor(0.95f, 0.06f, 0.08f, 1.0f) }),
	};
	DescriptionFormat = LOCTEXT("DescriptionFormat",
		"{Applicants} applicants across {Centers} active centers. {Approved} visas approved ({ProgressPercent}) and {Biometrics} biometric enrolments completed.");
	ProgressCaption = LOCTEXT("ProgressCaption", "APPROVAL RATE  {Percent}");
	HeadlineLabel = LOCTEXT("HeadlineLabel", "Total applicants");
	SampleBadgeText = LOCTEXT("SampleBadge", "SAMPLE DATA");

}

void UGlobeStatCardWidget::BuildDefaultLayout(UWidgetTree* Tree)
{
	// CardSize > CardLayers [ Blur > Glass > Lines ..., AccentLine (top edge) ]
	USizeBox* Size = Make<USizeBox>(Tree, TEXT("CardSize"));
	Tree->RootWidget = Size;
	UOverlay* Layers = Make<UOverlay>(Tree, TEXT("CardLayers"));
	Size->AddChild(Layers);

	UBackgroundBlur* BlurWidget = Make<UBackgroundBlur>(Tree, TEXT("Blur"));
	BlurWidget->SetPadding(FMargin(0.0f));
	BlurWidget->SetApplyAlphaToBlur(true); // fades with the card
	UOverlaySlot* BlurSlot = Layers->AddChildToOverlay(BlurWidget);
	BlurSlot->SetHorizontalAlignment(HAlign_Fill);
	BlurSlot->SetVerticalAlignment(VAlign_Fill);

	UBorder* GlassWidget = Make<UBorder>(Tree, TEXT("Glass"));
	BlurWidget->AddChild(GlassWidget);

	UImage* Line = Make<UImage>(Tree, TEXT("AccentLine"));
	UOverlaySlot* LineSlot = Layers->AddChildToOverlay(Line);
	LineSlot->SetHorizontalAlignment(HAlign_Fill);
	LineSlot->SetVerticalAlignment(VAlign_Top);

	UGlobeCardEdgeFlow* Flow = Make<UGlobeCardEdgeFlow>(Tree, TEXT("EdgeFlow"));
	UOverlaySlot* FlowSlot = Layers->AddChildToOverlay(Flow);
	FlowSlot->SetHorizontalAlignment(HAlign_Fill);
	FlowSlot->SetVerticalAlignment(VAlign_Fill);

	UVerticalBox* Lines = Make<UVerticalBox>(Tree, TEXT("Lines"));
	GlassWidget->SetContent(Lines);

	UHorizontalBox* Header = Make<UHorizontalBox>(Tree, TEXT("Header"));
	Lines->AddChildToVerticalBox(Header);
	UImage* Dot = Make<UImage>(Tree, TEXT("AccentDot"));
	UHorizontalBoxSlot* DotSlot = Header->AddChildToHorizontalBox(Dot);
	DotSlot->SetVerticalAlignment(VAlign_Center);
	DotSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Make<UTextBlock>(Tree, TEXT("TitleText")));
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* BadgeSlot = Header->AddChildToHorizontalBox(Make<UTextBlock>(Tree, TEXT("SampleBadge")));
	BadgeSlot->SetVerticalAlignment(VAlign_Center);
	BadgeSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));

	Lines->AddChildToVerticalBox(Make<UTextBlock>(Tree, TEXT("SubtitleText")))->SetPadding(FMargin(15.0f, 1.0f, 0.0f, 0.0f));
	Lines->AddChildToVerticalBox(Make<UTextBlock>(Tree, TEXT("ValueText")))->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	UTextBlock* Secondary = Make<UTextBlock>(Tree, TEXT("SecondaryText"));
	Secondary->SetAutoWrapText(true);
	Lines->AddChildToVerticalBox(Secondary);
	Lines->AddChildToVerticalBox(Make<UImage>(Tree, TEXT("Divider")))->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 8.0f));
	Lines->AddChildToVerticalBox(Make<UVerticalBox>(Tree, TEXT("StatsBox")));
	Lines->AddChildToVerticalBox(Make<UTextBlock>(Tree, TEXT("ProgressLabel")))->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 4.0f));
	Lines->AddChildToVerticalBox(Make<UProgressBar>(Tree, TEXT("ProgressBar")));
	UTextBlock* Description = Make<UTextBlock>(Tree, TEXT("DescriptionText"));
	Description->SetAutoWrapText(true);
	Lines->AddChildToVerticalBox(Description)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	UTextBlock* Details = Make<UTextBlock>(Tree, TEXT("DetailsText"));
	Details->SetAutoWrapText(true);
	Lines->AddChildToVerticalBox(Details)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
}

void UGlobeStatCardWidget::BindParts()
{
	if (!WidgetTree)
	{
		return;
	}
	// A Widget Blueprint binds these by name automatically; the plain C++ class finds them here.
	auto Find = [this](auto& Member, const TCHAR* Name)
	{
		using FType = std::remove_pointer_t<decltype(Member.Get())>;
		if (!Member)
		{
			Member = Cast<FType>(WidgetTree->FindWidget(FName(Name)));
		}
	};
	Find(CardSize, TEXT("CardSize"));
	Find(Blur, TEXT("Blur"));
	Find(Glass, TEXT("Glass"));
	Find(AccentLine, TEXT("AccentLine"));
	Find(AccentDot, TEXT("AccentDot"));
	Find(TitleText, TEXT("TitleText"));
	Find(SampleBadge, TEXT("SampleBadge"));
	Find(SubtitleText, TEXT("SubtitleText"));
	Find(ValueText, TEXT("ValueText"));
	Find(SecondaryText, TEXT("SecondaryText"));
	Find(Divider, TEXT("Divider"));
	Find(StatsBox, TEXT("StatsBox"));
	Find(ProgressLabel, TEXT("ProgressLabel"));
	Find(ProgressBar, TEXT("ProgressBar"));
	Find(DescriptionText, TEXT("DescriptionText"));
	Find(DetailsText, TEXT("DetailsText"));
	Find(EdgeFlow, TEXT("EdgeFlow"));
}

void UGlobeStatCardWidget::EnsureEdgeFlow()
{
	if (EdgeFlow || !WidgetTree)
	{
		return;
	}
	// Layouts made before the edge flow existed: put it over the card, on top of the glass.
	if (UOverlay* Layers = Cast<UOverlay>(WidgetTree->FindWidget(TEXT("CardLayers"))))
	{
		EdgeFlow = WidgetTree->ConstructWidget<UGlobeCardEdgeFlow>(UGlobeCardEdgeFlow::StaticClass(), TEXT("EdgeFlow"));
		UOverlaySlot* FlowSlot = Layers->AddChildToOverlay(EdgeFlow);
		FlowSlot->SetHorizontalAlignment(HAlign_Fill);
		FlowSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

void UGlobeStatCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout(WidgetTree); // plain C++ class, no Widget Blueprint
	}
	BindParts();
	EnsureEdgeFlow();
	ApplyStyle();
}

void UGlobeStatCardWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	BindParts();
	EnsureEdgeFlow();
	if (IsDesignTime())
	{
		// Designer: show the first country so every setting can be tuned on real content.
		SourceItem = FGlobeDataItem();
		SourceItem.Title = Countries.Num() > 0 ? Countries[0].Name : FText::FromString(TEXT("Lebanon"));
		ItemId = NAME_None;
		bSelected = false;
		bDetailed = false;
	}
	ApplyStyle();
}

void UGlobeStatCardWidget::SetData(const FGlobeDataItem& Item)
{
	SourceItem = Item;
	ItemId = Item.Id;
	ApplyStyle();
	OnDataChanged(Data);
}

void UGlobeStatCardWidget::SetAccentColor(FLinearColor Color)
{
	AccentColor = WithAlpha(Color, 1.0f);
	ApplyStyle();
}

void UGlobeStatCardWidget::SetSelected(bool bInSelected)
{
	if (bSelected != bInSelected)
	{
		bSelected = bInSelected;
		ApplyStyle();
		OnSelectionChanged(bSelected);
	}
}

void UGlobeStatCardWidget::SetDetailed(bool bInDetailed)
{
	if (bDetailed != bInDetailed)
	{
		bDetailed = bInDetailed;
		ApplyStyle(); // the detail card lists every stat
		OnDetailModeChanged(bDetailed);
	}
}

const FGlobeCardCountryData* UGlobeStatCardWidget::FindCountry(const FText& Title, FName Id) const
{
	const FString TitleString = Title.ToString().TrimStartAndEnd();
	for (const FGlobeCardCountryData& Country : Countries)
	{
		const FString Name = Country.Name.ToString().TrimStartAndEnd();
		if (!Name.IsEmpty() && (Name.Equals(TitleString, ESearchCase::IgnoreCase) || (!Id.IsNone() && Name.Equals(Id.ToString(), ESearchCase::IgnoreCase))))
		{
			return &Country;
		}
	}
	return nullptr;
}

FText UGlobeStatCardWidget::FormatNumber(int64 Value) const
{
	if (bCompactNumbers && FMath::Abs(Value) >= 10000)
	{
		FNumberFormattingOptions Options;
		Options.MaximumFractionalDigits = 1;
		const bool bMillions = FMath::Abs(Value) >= 1000000;
		const double Scaled = (double)Value / (bMillions ? 1000000.0 : 1000.0);
		return FText::Format(LOCTEXT("Compact", "{0}{1}"), FText::AsNumber(Scaled, &Options), FText::FromString(bMillions ? TEXT("M") : TEXT("K")));
	}
	return FText::AsNumber(Value);
}

void UGlobeStatCardWidget::ResolveData()
{
	Data = SourceItem;
	ProgressCaptionText = FText::GetEmpty();
	FlowColors.Reset();
	bShowSampleBadge = false;

	const FGlobeCardCountryData* Country = bUseCountryData ? FindCountry(SourceItem.Title, SourceItem.Id) : nullptr;
	if (!Country)
	{
		return; // the item's own data
	}
	if (!Country->Subtitle.IsEmpty())
	{
		Data.Subtitle = Country->Subtitle;
	}

	// Stats in category order, with their icons.
	Data.Stats.Reset();
	FFormatNamedArguments Args;
	Args.Add(TEXT("Country"), Data.Title);
	for (const FGlobeCardStatCategory& Category : Categories)
	{
		const int64* Value = Category.Key.IsNone() ? nullptr : Country->Values.Find(Category.Key);
		const FText Formatted = Value ? FormatNumber(*Value) : FText::FromString(TEXT("—"));
		if (!Category.Key.IsNone())
		{
			Args.Add(Category.Key.ToString(), Formatted);
		}
		if (Category.bShow)
		{
			FGlobeDataStat Stat(Category.Label, Formatted);
			Stat.Icon = Category.Icon;
			Stat.IconTexture = Category.IconTexture;
			Data.Stats.Add(Stat);
		}
	}

	// Headline number.
	if (const int64* Headline = HeadlineKey.IsNone() ? nullptr : Country->Values.Find(HeadlineKey))
	{
		Data.Value = FormatNumber(*Headline);
		Data.Secondary = HeadlineLabel;
	}

	// Progress (e.g. approved / applicants).
	const int64* Numerator = ProgressNumeratorKey.IsNone() ? nullptr : Country->Values.Find(ProgressNumeratorKey);
	const int64* Denominator = ProgressDenominatorKey.IsNone() ? nullptr : Country->Values.Find(ProgressDenominatorKey);
	FText Percent = FText::FromString(TEXT("—"));
	if (Numerator && Denominator && *Denominator > 0)
	{
		const double Ratio = FMath::Clamp((double)*Numerator / (double)*Denominator, 0.0, 1.0);
		FNumberFormattingOptions Options;
		Options.MaximumFractionalDigits = 0;
		Percent = FText::AsPercent(Ratio, &Options);
		Data.bHasProgress = true;
		Data.Progress = (float)Ratio;
		FFormatNamedArguments CaptionArgs;
		CaptionArgs.Add(TEXT("Percent"), Percent);
		ProgressCaptionText = FText::Format(ProgressCaption, CaptionArgs);
	}
	else
	{
		Data.bHasProgress = false;
	}
	Args.Add(TEXT("ProgressPercent"), Percent);

	// Activity summary.
	const FText& Format = Country->DescriptionOverride.IsEmpty() ? DescriptionFormat : Country->DescriptionOverride;
	if (!Format.IsEmpty())
	{
		Data.Description = FText::Format(Format, Args);
	}
	bShowSampleBadge = Country->bSampleData;
	FlowColors = Country->Colors.Num() > 0 ? Country->Colors : BuiltInFlagColors(Country->Name.ToString());
}

void UGlobeStatCardWidget::RebuildStats()
{
	if (!StatsBox || !WidgetTree)
	{
		return;
	}
	StatsBox->ClearChildren();
	const FLinearColor Shadow(0.0f, 0.0f, 0.0f, TextShadowOpacity);
	auto MakeText = [this, &Shadow](const FText& Text, const FSlateFontInfo& InFont, const FLinearColor& Color)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
		Block->SetText(Text);
		Block->SetFont(InFont);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetShadowOffset(FVector2D(0.0, 1.0));
		Block->SetShadowColorAndOpacity(Shadow);
		return Block;
	};
	auto MakeIcon = [this](const FGlobeDataStat& Stat) -> UWidget*
	{
		if (!bShowIcons || (Stat.Icon == EGlobeStatIcon::None && !Stat.IconTexture))
		{
			return nullptr;
		}
		UGlobeStatIcon* Icon = WidgetTree->ConstructWidget<UGlobeStatIcon>();
		Icon->Icon = Stat.Icon;
		Icon->Texture = Stat.IconTexture;
		Icon->Color = IconColor;
		Icon->Size = IconSize;
		Icon->LineThickness = IconLineThickness;
		Icon->DiscOpacity = IconDiscOpacity;
		return Icon;
	};

	const int32 Count = bDetailed ? Data.Stats.Num() : FMath::Min(Data.Stats.Num(), MaxCompactStats);
	if (StatsLayout == EGlobeCardStatsLayout::List)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const FGlobeDataStat& Stat = Data.Stats[i];
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			if (UWidget* Icon = MakeIcon(Stat))
			{
				UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(Icon);
				IconSlot->SetVerticalAlignment(VAlign_Center);
				IconSlot->SetPadding(FMargin(0.0f, 0.0f, IconGap, 0.0f));
			}
			UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(MakeText(Stat.Label, Font(TEXT("Regular"), StatSize), StatLabelColor));
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(MakeText(Stat.Value, Font(TEXT("Bold"), StatSize), StatValueColor));
			ValueSlot->SetHorizontalAlignment(HAlign_Right);
			ValueSlot->SetVerticalAlignment(VAlign_Center);
			ValueSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
			StatsBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, i > 0 ? StatRowSpacing : 0.0f, 0.0f, 0.0f));
		}
		return;
	}

	// Grid: rows of GridColumns tiles, each tile = icon + (value over label).
	const int32 Columns = FMath::Max(1, GridColumns);
	for (int32 First = 0; First < Count; First += Columns)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 c = 0; c < Columns; ++c)
		{
			const int32 i = First + c;
			UHorizontalBox* Tile = WidgetTree->ConstructWidget<UHorizontalBox>();
			if (i < Count)
			{
				const FGlobeDataStat& Stat = Data.Stats[i];
				if (UWidget* Icon = MakeIcon(Stat))
				{
					UHorizontalBoxSlot* IconSlot = Tile->AddChildToHorizontalBox(Icon);
					IconSlot->SetVerticalAlignment(VAlign_Center);
					IconSlot->SetPadding(FMargin(0.0f, 0.0f, IconGap, 0.0f));
				}
				UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>();
				Texts->AddChildToVerticalBox(MakeText(Stat.Value, Font(TEXT("Bold"), TileValueSize), StatValueColor));
				Texts->AddChildToVerticalBox(MakeText(Stat.Label, Font(TEXT("Regular"), TileLabelSize), StatLabelColor));
				Tile->AddChildToHorizontalBox(Texts)->SetVerticalAlignment(VAlign_Center);
			}
			UHorizontalBoxSlot* TileSlot = Row->AddChildToHorizontalBox(Tile);
			TileSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TileSlot->SetPadding(FMargin(c > 0 ? GridSpacing.X : 0.0f, 0.0f, 0.0f, 0.0f));
		}
		StatsBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, First > 0 ? GridSpacing.Y : 0.0f, 0.0f, 0.0f));
	}
}

void UGlobeStatCardWidget::ApplyStyle()
{
	Accent = WithAlpha(AccentColor, 1.0f);
	ResolveData();
	RebuildStats();

	const FLinearColor Shadow(0.0f, 0.0f, 0.0f, TextShadowOpacity);
	const bool bTextColors = bUseCardTextColors;
	auto StyleText = [&Shadow, bTextColors](UTextBlock* Text, const FSlateFontInfo& InFont, const FLinearColor& Color)
	{
		if (Text)
		{
			Text->SetFont(InFont);
			if (bTextColors)
			{
				Text->SetColorAndOpacity(FSlateColor(Color)); // off: keeps the colour set on the widget in the Designer
			}
			Text->SetShadowOffset(FVector2D(0.0, 1.0));
			Text->SetShadowColorAndOpacity(Shadow);
		}
	};

	if (CardSize)
	{
		CardSize->SetMinDesiredWidth(bDetailed ? DetailMinWidth : CardMinWidth);
		CardSize->SetMaxDesiredWidth(FMath::Max(bDetailed ? DetailMaxWidth : CardMaxWidth, bDetailed ? DetailMinWidth : CardMinWidth));
	}
	if (Blur)
	{
		Blur->SetBlurStrength(BlurStrength);
		Blur->SetCornerRadius(FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius));
	}
	if (Glass)
	{
		// Tinted translucent fill with a thin rim; the rim brightens (and takes the accent) when selected.
		const FLinearColor Rim = bBorderUsesAccent ? FMath::Lerp(Accent, FLinearColor::White, 0.35f) : BorderColor;
		const float RimOpacity = bSelected ? SelectedBorderOpacity : BorderOpacity;
		const FLinearColor Fill = bSelected ? FMath::Lerp(GlassTint, Accent * 0.35f, 0.25f) : GlassTint;
		Glass->SetBrush(FSlateRoundedBoxBrush(WithAlpha(Fill, GlassOpacity), CornerRadius, WithAlpha(Rim, RimOpacity), BorderThickness));
		Glass->SetBrushColor(FLinearColor::White);
		Glass->SetPadding(bDetailed ? DetailPadding : CardPadding);
	}
	if (AccentLine)
	{
		FSlateRoundedBoxBrush LineBrush(FLinearColor::White, AccentLineThickness * 0.5f);
		LineBrush.ImageSize = FVector2f(8.0f, AccentLineThickness);
		AccentLine->SetBrush(LineBrush);
		AccentLine->SetColorAndOpacity(WithAlpha(Accent, AccentLineOpacity));
		if (UOverlaySlot* LineSlot = Cast<UOverlaySlot>(AccentLine->Slot))
		{
			LineSlot->SetPadding(FMargin(CornerRadius + 4.0f, 0.0f)); // stays on the straight part of the top edge
		}
		SetShown(AccentLine, AccentLineThickness > 0.0f && AccentLineOpacity > 0.0f);
	}
	if (EdgeFlow)
	{
		EdgeFlow->Colors = bShowEdgeFlow ? FlowColors : TArray<FLinearColor>();
		EdgeFlow->ParticleCount = EdgeParticleCount;
		EdgeFlow->Speed = EdgeSpeed;
		EdgeFlow->ParticleSize = EdgeParticleSize;
		EdgeFlow->TrailLength = EdgeTrailLength;
		EdgeFlow->TrailSpacing = EdgeTrailSpacing;
		EdgeFlow->Opacity = EdgeOpacity;
		EdgeFlow->Glow = EdgeGlow;
		EdgeFlow->CornerRadius = CornerRadius;
		EdgeFlow->Inset = EdgeInset;
		EdgeFlow->Clumping = EdgeClumping;
	}
	if (AccentDot)
	{
		const float DotSize = FMath::Max(4.0f, TitleSize * 0.45f);
		FSlateRoundedBoxBrush DotBrush(FLinearColor::White, DotSize * 0.5f);
		DotBrush.ImageSize = FVector2f(DotSize, DotSize);
		AccentDot->SetBrush(DotBrush);
		AccentDot->SetColorAndOpacity(Accent);
	}

	const FLinearColor Title = FMath::Lerp(TitleColor, FMath::Lerp(Accent, FLinearColor::White, 0.4f), TitleAccentTint);
	StyleText(TitleText, Font(TEXT("Bold"), bDetailed ? FMath::RoundToInt32(TitleSize * 1.2f) : TitleSize), WithAlpha(Title, 1.0f));
	StyleText(SampleBadge, Font(TEXT("Bold"), CaptionSize, 120), SampleBadgeColor);
	StyleText(SubtitleText, Font(TEXT("Regular"), SubtitleSize, bUppercaseSubtitle ? 160 : 0), SubtitleColor);
	StyleText(ValueText, Font(TEXT("Light"), bDetailed ? DetailValueSize : ValueSize), ValueColor);
	StyleText(SecondaryText, Font(TEXT("Regular"), SecondarySize), SecondaryColor);
	StyleText(ProgressLabel, Font(TEXT("Bold"), CaptionSize, 120), StatLabelColor);
	StyleText(DescriptionText, Font(TEXT("Regular"), DescriptionSize), DescriptionColor);
	StyleText(DetailsText, Font(TEXT("Regular"), DetailsSize), DetailsColor);

	if (TitleText)
	{
		TitleText->SetText(Data.Title);
	}
	if (SampleBadge)
	{
		SampleBadge->SetText(SampleBadgeText);
		SetShown(SampleBadge, bShowSampleBadge && !SampleBadgeText.IsEmpty());
	}
	if (SubtitleText)
	{
		SubtitleText->SetText(bUppercaseSubtitle ? Data.Subtitle.ToUpper() : Data.Subtitle);
		SetShown(SubtitleText, !Data.Subtitle.IsEmpty());
	}
	if (ValueText)
	{
		ValueText->SetText(Data.Value);
		SetShown(ValueText, !Data.Value.IsEmpty());
	}
	if (SecondaryText)
	{
		SecondaryText->SetText(Data.Secondary);
		SetShown(SecondaryText, !Data.Secondary.IsEmpty());
	}
	if (DescriptionText)
	{
		DescriptionText->SetText(Data.Description);
		SetShown(DescriptionText, !Data.Description.IsEmpty());
	}
	if (DetailsText)
	{
		DetailsText->SetText(Data.Details);
		SetShown(DetailsText, bDetailed && !Data.Details.IsEmpty());
	}

	const bool bHasStats = StatsBox && StatsBox->GetChildrenCount() > 0;
	SetShown(StatsBox, bHasStats);
	if (Divider)
	{
		FSlateBrush DividerBrush;
		DividerBrush.DrawAs = ESlateBrushDrawType::Box;
		DividerBrush.ImageSize = FVector2f(8.0f, 1.0f);
		Divider->SetBrush(DividerBrush);
		Divider->SetColorAndOpacity(WithAlpha(FMath::Lerp(StatLabelColor, Accent, 0.3f), 0.28f));
		SetShown(Divider, bHasStats);
	}
	const bool bCaption = Data.bHasProgress && !ProgressCaptionText.IsEmpty();
	if (ProgressLabel)
	{
		ProgressLabel->SetText(ProgressCaptionText);
		SetShown(ProgressLabel, bCaption);
	}
	if (ProgressBar)
	{
		FProgressBarStyle BarStyle;
		FSlateRoundedBoxBrush Track(FLinearColor(1.0f, 1.0f, 1.0f, 0.09f), 2.0f);
		Track.ImageSize = FVector2f(8.0f, 4.0f);
		FSlateRoundedBoxBrush FillBrush(FLinearColor::White, 2.0f);
		FillBrush.ImageSize = FVector2f(8.0f, 4.0f);
		BarStyle.SetBackgroundImage(Track);
		BarStyle.SetFillImage(FillBrush);
		BarStyle.SetMarqueeImage(FillBrush);
		ProgressBar->SetWidgetStyle(BarStyle);
		ProgressBar->SetFillColorAndOpacity(Accent);
		ProgressBar->SetPercent(FMath::Clamp(Data.Progress, 0.0f, 1.0f));
		SetShown(ProgressBar, Data.bHasProgress);
		if (UVerticalBoxSlot* BarSlot = Cast<UVerticalBoxSlot>(ProgressBar->Slot))
		{
			BarSlot->SetPadding(FMargin(0.0f, ProgressLabel && bCaption ? 0.0f : 10.0f, 0.0f, 0.0f));
		}
	}
}

FReply UGlobeStatCardWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnCardClicked.Broadcast(ItemId);
		return FReply::Handled(); // the click selects the card and does not start a camera orbit
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

#undef LOCTEXT_NAMESPACE
