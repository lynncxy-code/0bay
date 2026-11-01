#include "UI/OntoTwinGlassRenderer.h"

#include "DynamicRHI.h"
#include "FX/SlateFXSubsystem.h"
#include "FX/SlateRHIPostBufferProcessor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SlateRendererTypes.h"
#include "SlateRHIRendererSettings.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogOntoTwinGlass);

namespace OntoTwinGlassRendererPrivate
{
	static TAutoConsoleVariable<int32> CVarEnable(
		TEXT("OntoTwin.Glass.Enable"),
		-1,
		TEXT("Override the glass UI setting. -1: project setting, 0: Performance surface, 1: enabled."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarForceQuality(
		TEXT("OntoTwin.Glass.ForceQuality"),
		-1,
		TEXT("Override requested glass quality. -1: project setting, 0: Performance, 1: Balanced, 2: High."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarDump(
		TEXT("OntoTwin.Glass.Dump"),
		0,
		TEXT("Set to 1 to log the next resolved glass renderer decision."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarReduceMotion(
		TEXT("OntoTwin.Glass.ReduceMotion"),
		-1,
		TEXT("Override reduced motion. -1: project setting, 0: off, 1: on."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarReduceTransparency(
		TEXT("OntoTwin.Glass.ReduceTransparency"),
		-1,
		TEXT("Override reduced transparency. -1: project setting, 0: off, 1: force Performance."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarHighContrast(
		TEXT("OntoTwin.Glass.HighContrast"),
		-1,
		TEXT("Override high contrast. -1: project setting, 0: off, 1: on."),
		ECVF_Default);

	static FAutoConsoleCommand CmdDiagnose(
		TEXT("OntoTwin.Glass.Diagnose"),
		TEXT("Resolve and log both Screen and World glass renderer decisions."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FOntoTwinGlassRenderer::Resolve(false);
			FOntoTwinGlassRenderer::Resolve(true);
		}));

	static ESlatePostRT PostBufferFromIndex(const int32 Index)
	{
		switch (FMath::Clamp(Index, 0, 4))
		{
		case 1: return ESlatePostRT::ESlatePostRT_1;
		case 2: return ESlatePostRT::ESlatePostRT_2;
		case 3: return ESlatePostRT::ESlatePostRT_3;
		case 4: return ESlatePostRT::ESlatePostRT_4;
		default: return ESlatePostRT::ESlatePostRT_0;
		}
	}

	static bool IsGlassEnabled(const UOntoTwinGlassSettings* Settings)
	{
		const int32 Override = CVarEnable.GetValueOnGameThread();
		if (Override == 0)
		{
			return false;
		}
		if (Override == 1)
		{
			return true;
		}
		return !Settings || Settings->bEnableGlassUI;
	}

	static bool IsHighRendererEnabled(const UOntoTwinGlassSettings* Settings)
	{
		return Settings
			&& (Settings->bEnableHighQualityRenderer || Settings->bEnableRendererSpike);
	}

	static bool ResolveAccessibilityFlag(
		const TAutoConsoleVariable<int32>& Override,
		const bool ProjectValue)
	{
		const int32 Value = Override.GetValueOnGameThread();
		if (Value == 0) return false;
		if (Value == 1) return true;
		return ProjectValue;
	}

	static bool IsReduceMotionEnabled(const UOntoTwinGlassSettings* Settings)
	{
		return ResolveAccessibilityFlag(
			CVarReduceMotion,
			Settings && Settings->bReduceMotion);
	}

	static bool IsReduceTransparencyEnabled(const UOntoTwinGlassSettings* Settings)
	{
		return ResolveAccessibilityFlag(
			CVarReduceTransparency,
			Settings && Settings->bReduceTransparency);
	}

	static bool IsHighContrastEnabled(const UOntoTwinGlassSettings* Settings)
	{
		return ResolveAccessibilityFlag(
			CVarHighContrast,
			Settings && Settings->bHighContrast);
	}

	static EOntoTwinGlassQuality ApplyQualityOverride(
		const EOntoTwinGlassQuality RequestedQuality)
	{
		switch (CVarForceQuality.GetValueOnGameThread())
		{
		case 0: return EOntoTwinGlassQuality::Performance;
		case 1: return EOntoTwinGlassQuality::Balanced;
		case 2: return EOntoTwinGlassQuality::High;
		default: return RequestedQuality;
		}
	}

	static bool IsConsoleVariableEnabled(const TCHAR* Name, const bool bDefaultIfMissing)
	{
		if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return Variable->GetInt() != 0;
		}
		return bDefaultIfMissing;
	}

	static bool IsBalancedAvailable(FString& OutReason)
	{
		if (!IsConsoleVariableEnabled(TEXT("Slate.AllowBackgroundBlurWidgets"), false))
		{
			OutReason = TEXT("Slate background blur widgets are disabled or unavailable");
			return false;
		}

		if (IsConsoleVariableEnabled(TEXT("Slate.ForceBackgroundBlurLowQualityOverride"), false))
		{
			OutReason = TEXT("Slate background blur is forced to its low-quality brush fallback");
			return false;
		}

		return true;
	}

	static UMaterialInterface* ResolveHighMaterial(const int32 Index)
	{
		const FString AssetName = FString::Printf(TEXT("M_OT_GlassHigh_RT%d"), Index);
		const FString ObjectPath = FString::Printf(
			TEXT("/OntoTwinSync/UI/RendererSpike/%s.%s"),
			*AssetName,
			*AssetName);
		return LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
	}

	static UMaterialInterface* ResolveHighRequirements(const int32 Index, FString& OutReason)
	{
		if (!GDynamicRHI || RHIGetInterfaceType() != ERHIInterfaceType::D3D12)
		{
			OutReason = TEXT("High renderer requires the D3D12 RHI");
			return nullptr;
		}

		if (!IsConsoleVariableEnabled(TEXT("Slate.CopyBackbufferToSlatePostRenderTargets"), false))
		{
			OutReason = TEXT("Slate postbuffer backbuffer copy is disabled");
			return nullptr;
		}

		const USlateRHIRendererSettings* RendererSettings = GetDefault<USlateRHIRendererSettings>();
		if (!RendererSettings)
		{
			OutReason = TEXT("Slate RHI renderer settings are unavailable");
			return nullptr;
		}

		const ESlatePostRT PostBuffer = PostBufferFromIndex(Index);
		const FSlatePostSettings& PostSettings = RendererSettings->GetSlatePostSetting(PostBuffer);
		if (!PostSettings.bEnabled)
		{
			OutReason = FString::Printf(TEXT("Slate postbuffer RT%d is not enabled"), Index);
			return nullptr;
		}

		USlateRHIPostBufferProcessor* Processor = USlateFXSubsystem::GetPostProcessor(PostBuffer);
		if (!Processor || !Processor->IsA<UOntoTwinSlatePostBufferBlur>())
		{
			OutReason = FString::Printf(TEXT("OntoTwin blur processor is not instantiated for RT%d"), Index);
			return nullptr;
		}

		UMaterialInterface* Material = ResolveHighMaterial(Index);
		if (!Material)
		{
			OutReason = FString::Printf(TEXT("Matching High UI material M_OT_GlassHigh_RT%d is unavailable"), Index);
			return nullptr;
		}

		return Material;
	}

	static FString CurrentRHIName()
	{
		if (!GDynamicRHI)
		{
			return TEXT("Unavailable");
		}

		switch (RHIGetInterfaceType())
		{
		case ERHIInterfaceType::D3D11: return TEXT("D3D11");
		case ERHIInterfaceType::D3D12: return TEXT("D3D12");
		case ERHIInterfaceType::Vulkan: return TEXT("Vulkan");
		case ERHIInterfaceType::Metal: return TEXT("Metal");
		case ERHIInterfaceType::OpenGL: return TEXT("OpenGL");
		case ERHIInterfaceType::Null: return TEXT("Null");
		default: return TEXT("Other");
		}
	}

	static void LogDecision(const FOntoTwinGlassDecision& Decision, const bool bWorldSpace)
	{
		const FString PostBufferLabel = !bWorldSpace
			&& Decision.EffectiveQuality == EOntoTwinGlassQuality::High
			? FString::Printf(TEXT("RT%d"), Decision.PostBufferIndex)
			: TEXT("None");
		const FString Key = FString::Printf(
			TEXT("%s|%s|%s|%s|%s|%s"),
			bWorldSpace ? TEXT("World") : TEXT("Screen"),
			*FOntoTwinGlassRenderer::QualityToString(Decision.RequestedQuality),
			*FOntoTwinGlassRenderer::QualityToString(Decision.EffectiveQuality),
			*PostBufferLabel,
			*CurrentRHIName(),
			*Decision.DegradeReason);

		static FString LastScreenDecision;
		static FString LastWorldDecision;
		FString& LastDecision = bWorldSpace ? LastWorldDecision : LastScreenDecision;
		const bool bDumpRequested = CVarDump.GetValueOnGameThread() != 0;
		if (Key != LastDecision || bDumpRequested)
		{
			UE_LOG(
				LogOntoTwinGlass,
				Log,
				TEXT("context=%s requested=%s effective=%s postbuffer=%s rhi=%s material=%s reason=%s"),
				bWorldSpace ? TEXT("World") : TEXT("Screen"),
				*FOntoTwinGlassRenderer::QualityToString(Decision.RequestedQuality),
				*FOntoTwinGlassRenderer::QualityToString(Decision.EffectiveQuality),
				*PostBufferLabel,
				*CurrentRHIName(),
				Decision.HighMaterial ? *Decision.HighMaterial->GetPathName() : TEXT("None"),
				Decision.DegradeReason.IsEmpty() ? TEXT("None") : *Decision.DegradeReason);
			LastDecision = Key;
		}

		if (bDumpRequested)
		{
			CVarDump->Set(0, ECVF_SetByCode);
		}
	}
}

UOntoTwinGlassSettings::UOntoTwinGlassSettings()
{
	bEnableGlassUI = true;
	bEnableHighQualityRenderer = false;
	bEnableRendererSpike = false;
	RequestedQuality = EOntoTwinGlassQuality::Balanced;
	ReservedPostBufferIndex = 0;
	GaussianBlurStrength = 14.0f;
	bReduceMotion = false;
	bReduceTransparency = false;
	bHighContrast = false;
}

UOntoTwinSlatePostBufferBlur::UOntoTwinSlatePostBufferBlur()
{
	if (const UOntoTwinGlassSettings* Settings = GetDefault<UOntoTwinGlassSettings>())
	{
		GaussianBlurStrength = FMath::Max(0.0f, Settings->GaussianBlurStrength);
	}
}

void FOntoTwinGlassRenderer::ConfigureSlatePostBuffer()
{
	using namespace OntoTwinGlassRendererPrivate;

	const UOntoTwinGlassSettings* GlassSettings = GetDefault<UOntoTwinGlassSettings>();
	if (!IsGlassEnabled(GlassSettings))
	{
		UE_LOG(LogOntoTwinGlass, Log, TEXT("Glass UI is disabled; Slate postbuffer settings were not modified."));
		return;
	}
	if (!IsHighRendererEnabled(GlassSettings))
	{
		UE_LOG(LogOntoTwinGlass, Log, TEXT("High Screen capability is disabled; no Slate postbuffer was reserved."));
		return;
	}

	const int32 Index = FMath::Clamp(GlassSettings ? GlassSettings->ReservedPostBufferIndex : 0, 0, 4);
	USlateRHIRendererSettings* RendererSettings = GetMutableDefault<USlateRHIRendererSettings>();
	if (!RendererSettings)
	{
		UE_LOG(LogOntoTwinGlass, Warning, TEXT("Cannot reserve RT%d because Slate RHI renderer settings are unavailable."), Index);
		return;
	}

	FSlatePostSettings& PostSettings = RendererSettings->GetMutableSlatePostSetting(PostBufferFromIndex(Index));
	UClass* ExistingProcessorClass = PostSettings.PostProcessorClass.Get();
	if (ExistingProcessorClass && !ExistingProcessorClass->IsChildOf(UOntoTwinSlatePostBufferBlur::StaticClass()))
	{
		UE_LOG(
			LogOntoTwinGlass,
			Warning,
			TEXT("RT%d is configured with foreign processor %s; OntoTwin will not overwrite or enable that slot."),
			Index,
			*ExistingProcessorClass->GetPathName());
		return;
	}

	PostSettings.bEnabled = true;
	if (!ExistingProcessorClass)
	{
		PostSettings.PostProcessorClass = UOntoTwinSlatePostBufferBlur::StaticClass();
		ExistingProcessorClass = UOntoTwinSlatePostBufferBlur::StaticClass();
	}

	if (USlatePostBufferBlur* ProcessorDefaults = Cast<USlatePostBufferBlur>(ExistingProcessorClass->GetDefaultObject()))
	{
		ProcessorDefaults->GaussianBlurStrength =
			FMath::Max(0.0f, GlassSettings ? GlassSettings->GaussianBlurStrength : 14.0f);
	}

	UE_LOG(
		LogOntoTwinGlass,
		Log,
		TEXT("Reserved shared Slate postbuffer RT%d with %s (blur %.2f)."),
		Index,
		*ExistingProcessorClass->GetPathName(),
		CastChecked<USlatePostBufferBlur>(ExistingProcessorClass->GetDefaultObject())->GaussianBlurStrength);
}

FOntoTwinGlassDecision FOntoTwinGlassRenderer::Resolve(const bool bWorldSpace)
{
	const UOntoTwinGlassSettings* Settings = GetDefault<UOntoTwinGlassSettings>();
	return Resolve(
		bWorldSpace,
		Settings ? Settings->RequestedQuality : EOntoTwinGlassQuality::Balanced);
}

FOntoTwinGlassDecision FOntoTwinGlassRenderer::Resolve(
	const bool bWorldSpace,
	const EOntoTwinGlassQuality RequestedQuality)
{
	using namespace OntoTwinGlassRendererPrivate;

	const UOntoTwinGlassSettings* Settings = GetDefault<UOntoTwinGlassSettings>();
	FOntoTwinGlassDecision Decision;
	Decision.RequestedQuality = ApplyQualityOverride(RequestedQuality);
	Decision.EffectiveQuality = EOntoTwinGlassQuality::Performance;
	Decision.PostBufferIndex = FMath::Clamp(Settings ? Settings->ReservedPostBufferIndex : 0, 0, 4);

	if (IsReduceTransparencyEnabled(Settings))
	{
		Decision.DegradeReason = TEXT("ReduceTransparency forces the Performance surface");
		LogDecision(Decision, bWorldSpace);
		return Decision;
	}

	if (!IsGlassEnabled(Settings))
	{
		Decision.DegradeReason = TEXT("Glass UI is disabled");
		LogDecision(Decision, bWorldSpace);
		return Decision;
	}

	if (bWorldSpace)
	{
		Decision.EffectiveQuality = Decision.RequestedQuality;
		Decision.DegradeReason = TEXT("World Space uses deterministic pseudo-glass and never samples a Slate postbuffer");
		LogDecision(Decision, true);
		return Decision;
	}

	if (Decision.RequestedQuality == EOntoTwinGlassQuality::High)
	{
		FString HighFailure;
		UMaterialInterface* Material = nullptr;
		if (!IsHighRendererEnabled(Settings))
		{
			HighFailure = TEXT("High Screen capability is not enabled by the host project");
		}
		else
		{
			Material = ResolveHighRequirements(Decision.PostBufferIndex, HighFailure);
		}
		if (Material)
		{
			Decision.EffectiveQuality = EOntoTwinGlassQuality::High;
			Decision.HighMaterial = Material;
			LogDecision(Decision, false);
			return Decision;
		}

		FString BalancedFailure;
		if (IsBalancedAvailable(BalancedFailure))
		{
			Decision.EffectiveQuality = EOntoTwinGlassQuality::Balanced;
			Decision.DegradeReason = FString::Printf(TEXT("High downgraded to Balanced: %s"), *HighFailure);
		}
		else
		{
			Decision.DegradeReason = FString::Printf(
				TEXT("High downgraded to Performance: %s; %s"),
				*HighFailure,
				*BalancedFailure);
		}

		LogDecision(Decision, false);
		return Decision;
	}

	if (Decision.RequestedQuality == EOntoTwinGlassQuality::Balanced)
	{
		FString BalancedFailure;
		if (IsBalancedAvailable(BalancedFailure))
		{
			Decision.EffectiveQuality = EOntoTwinGlassQuality::Balanced;
		}
		else
		{
			Decision.DegradeReason = FString::Printf(TEXT("Balanced downgraded to Performance: %s"), *BalancedFailure);
		}
		LogDecision(Decision, false);
		return Decision;
	}

	Decision.DegradeReason = TEXT("Performance was explicitly requested");
	LogDecision(Decision, false);
	return Decision;
}

bool FOntoTwinGlassRenderer::ShouldReduceMotion()
{
	return OntoTwinGlassRendererPrivate::IsReduceMotionEnabled(
		GetDefault<UOntoTwinGlassSettings>());
}

bool FOntoTwinGlassRenderer::ShouldReduceTransparency()
{
	return OntoTwinGlassRendererPrivate::IsReduceTransparencyEnabled(
		GetDefault<UOntoTwinGlassSettings>());
}

bool FOntoTwinGlassRenderer::ShouldUseHighContrast()
{
	return OntoTwinGlassRendererPrivate::IsHighContrastEnabled(
		GetDefault<UOntoTwinGlassSettings>());
}

FString FOntoTwinGlassRenderer::QualityToString(const EOntoTwinGlassQuality Quality)
{
	switch (Quality)
	{
	case EOntoTwinGlassQuality::High: return TEXT("High");
	case EOntoTwinGlassQuality::Balanced: return TEXT("Balanced");
	default: return TEXT("Performance");
	}
}
