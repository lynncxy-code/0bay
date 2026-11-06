#include "UI/OntoTwinGlassTheme.h"

#include "Fonts/CompositeFont.h"
#include "Engine/Texture2D.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogOntoTwinGlassTheme, Log, All);

namespace OntoTwinGlassThemePrivate
{
	const FName RegularTypeface(TEXT("Regular"));
	const FName SemiBoldTypeface(TEXT("SemiBold"));
	TStrongObjectPtr<UTexture2D> SharedFineNoiseTexture;

	TSharedPtr<const FCompositeFont> BuildCompositeFont()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OntoTwinSync"));
		if (!Plugin.IsValid())
		{
			UE_LOG(LogOntoTwinGlassTheme, Warning,
				TEXT("OntoTwinSync plugin root was not found; falling back to the engine font."));
			return nullptr;
		}

		const FString FontRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Fonts"));
		const FString InterRegular = FPaths::Combine(FontRoot, TEXT("Inter-Regular.ttf"));
		const FString InterSemiBold = FPaths::Combine(FontRoot, TEXT("Inter-SemiBold.ttf"));
		const FString NotoRegular = FPaths::Combine(FontRoot, TEXT("NotoSansCJKsc-Regular.otf"));
		const FString NotoSemiBold = FPaths::Combine(FontRoot, TEXT("NotoSansCJKsc-Medium.otf"));
		if (!FPaths::FileExists(InterRegular)
			|| !FPaths::FileExists(InterSemiBold)
			|| !FPaths::FileExists(NotoRegular)
			|| !FPaths::FileExists(NotoSemiBold))
		{
			UE_LOG(LogOntoTwinGlassTheme, Warning,
				TEXT("Packaged OntoTwin font files are incomplete under %s; falling back to the engine font."),
				*FontRoot);
			return nullptr;
		}

		TSharedPtr<FStandaloneCompositeFont> Composite = MakeShared<FStandaloneCompositeFont>();
		Composite->DefaultTypeface
			.AppendFont(RegularTypeface, InterRegular, EFontHinting::Default, EFontLoadingPolicy::LazyLoad)
			.AppendFont(SemiBoldTypeface, InterSemiBold, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		Composite->FallbackTypeface.Typeface
			.AppendFont(RegularTypeface, NotoRegular, EFontHinting::Default, EFontLoadingPolicy::LazyLoad)
			.AppendFont(SemiBoldTypeface, NotoSemiBold, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		Composite->FallbackTypeface.ScalingFactor = 1.0f;
		return Composite;
	}

	TSharedPtr<const FCompositeFont> GetCompositeFont()
	{
		static TSharedPtr<const FCompositeFont> Composite = BuildCompositeFont();
		return Composite;
	}

	UTexture2D* BuildFineNoiseTexture()
	{
		constexpr int32 TextureSize = 32;
		UTexture2D* Texture = UTexture2D::CreateTransient(
			TextureSize,
			TextureSize,
			PF_B8G8R8A8,
			TEXT("OntoTwinFineNoise"));
		if (!Texture || !Texture->GetPlatformData()
			|| Texture->GetPlatformData()->Mips.IsEmpty())
		{
			return nullptr;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		if (!Pixels)
		{
			Mip.BulkData.Unlock();
			return nullptr;
		}

		uint32 Seed = 0x4F6E746Fu;
		for (int32 Index = 0; Index < TextureSize * TextureSize; ++Index)
		{
			Seed ^= Seed << 13;
			Seed ^= Seed >> 17;
			Seed ^= Seed << 5;
			const uint8 Alpha = static_cast<uint8>(64u + (Seed & 0x7Fu));
			Pixels[Index] = FColor(255, 255, 255, Alpha);
		}
		Mip.BulkData.Unlock();

		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->Filter = TF_Nearest;
		Texture->SRGB = false;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		return Texture;
	}
}

FSlateFontInfo FOntoTwinGlassTheme::Font(const float Size, const bool bSemibold)
{
	if (const TSharedPtr<const FCompositeFont> Composite =
		OntoTwinGlassThemePrivate::GetCompositeFont())
	{
		return FSlateFontInfo(
			Composite,
			Size,
			bSemibold
				? OntoTwinGlassThemePrivate::SemiBoldTypeface
				: OntoTwinGlassThemePrivate::RegularTypeface);
	}

	return FCoreStyle::GetDefaultFontStyle(bSemibold ? TEXT("Bold") : TEXT("Regular"), Size);
}

UTexture2D* FOntoTwinGlassTheme::FineNoiseTexture()
{
	if (!OntoTwinGlassThemePrivate::SharedFineNoiseTexture.IsValid())
	{
		OntoTwinGlassThemePrivate::SharedFineNoiseTexture.Reset(
			OntoTwinGlassThemePrivate::BuildFineNoiseTexture());
	}
	return OntoTwinGlassThemePrivate::SharedFineNoiseTexture.Get();
}

FLinearColor FOntoTwinGlassTheme::PrimaryText()
{
	return FLinearColor(0.96f, 0.96f, 0.96f, 1.0f);
}

FLinearColor FOntoTwinGlassTheme::SecondaryText()
{
	return FLinearColor(0.70f, 0.70f, 0.70f, 1.0f);
}

FLinearColor FOntoTwinGlassTheme::MutedText()
{
	return FLinearColor(0.52f, 0.52f, 0.52f, 1.0f);
}

FLinearColor FOntoTwinGlassTheme::Rim()
{
	return FLinearColor(1.0f, 1.0f, 1.0f, 0.24f);
}

FLinearColor FOntoTwinGlassTheme::ScreenTint(const EOntoTwinGlassQuality Quality)
{
	if (Quality == EOntoTwinGlassQuality::High)
	{
		return FLinearColor(0.045f, 0.045f, 0.045f, 0.34f);
	}
	if (Quality == EOntoTwinGlassQuality::Balanced)
	{
		return FLinearColor(0.045f, 0.045f, 0.045f, 0.48f);
	}
	return FLinearColor(0.040f, 0.040f, 0.040f, 0.82f);
}

FLinearColor FOntoTwinGlassTheme::WorldTint(const EOntoTwinGlassQuality Quality)
{
	if (Quality == EOntoTwinGlassQuality::High)
	{
		return FLinearColor(0.045f, 0.045f, 0.045f, 0.62f);
	}
	if (Quality == EOntoTwinGlassQuality::Balanced)
	{
		return FLinearColor(0.040f, 0.040f, 0.040f, 0.72f);
	}
	return FLinearColor(0.030f, 0.030f, 0.030f, 0.88f);
}

FLinearColor FOntoTwinGlassTheme::StatusAccent(const FString& Level)
{
	if (Level == TEXT("normal") || Level == TEXT("green")) return FLinearColor(0.08f, 0.55f, 0.33f, 1.0f);
	if (Level == TEXT("info") || Level == TEXT("cyan")) return FLinearColor(0.68f, 0.68f, 0.68f, 1.0f);
	if (Level == TEXT("warning") || Level == TEXT("amber")) return FLinearColor(0.80f, 0.52f, 0.12f, 1.0f);
	if (Level == TEXT("critical") || Level == TEXT("red")) return FLinearColor(0.72f, 0.18f, 0.16f, 1.0f);
	if (Level == TEXT("offline") || Level == TEXT("unknown") || Level == TEXT("gray")) return FLinearColor(0.46f, 0.46f, 0.46f, 1.0f);
	return MutedText();
}

FString FOntoTwinGlassTheme::StatusLabel(const FString& Level)
{
	if (Level == TEXT("normal")) return TEXT("在线");
	if (Level == TEXT("info")) return TEXT("信息");
	if (Level == TEXT("warning")) return TEXT("注意");
	if (Level == TEXT("critical")) return TEXT("告警");
	if (Level == TEXT("offline")) return TEXT("离线");
	return TEXT("未知");
}
