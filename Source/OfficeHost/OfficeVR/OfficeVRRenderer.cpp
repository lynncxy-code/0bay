#include "OfficeVR/OfficeVRRenderer.h"
#include "OfficeHost.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "IHeadMountedDisplay.h"
#include "ISpectatorScreenController.h"
#include "IXRTrackingSystem.h"

static TAutoConsoleVariable<int32> CVarOfficeVRLowGI(
	TEXT("officevr.LowGI"),
	0,
	TEXT("0=keep project GI (recommended), 1=lower Lumen quality, 2=disable Lumen and extra shadows."),
	ECVF_Default);

void ApplyOfficeVRRendererTweaks()
{
	auto SetCVarInt = [](const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	};
	auto SetCVarFloat = [](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	};

	const int32 Mode = CVarOfficeVRLowGI.GetValueOnGameThread();
	if (Mode >= 2)
	{
		SetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"), 0);
		SetCVarInt(TEXT("r.ReflectionMethod"), 2);
		SetCVarInt(TEXT("r.Shadow.Virtual.Enable"), 0);
		SetCVarInt(TEXT("r.ShadowQuality"), 2);
		SetCVarInt(TEXT("r.Lumen.DiffuseIndirect.Allow"), 0);
		SetCVarFloat(TEXT("r.ScreenPercentage"), 80.f);
		UE_LOG(LogOfficeVR, Display, TEXT("VR streaming: Lumen off, shadows reduced (officevr.LowGI=2)."));
	}
	else if (Mode == 1)
	{
		SetCVarFloat(TEXT("r.Lumen.FinalGatherQuality"), 0.5f);
		SetCVarInt(TEXT("r.Lumen.TraceMeshSDFs"), 0);
		UE_LOG(LogOfficeVR, Display, TEXT("VR streaming: Lumen quality reduced (officevr.LowGI=1)."));
	}
	else
	{
		UE_LOG(LogOfficeVR, Display, TEXT("VR streaming: project GI unchanged (officevr.LowGI=0)."));
	}

	if (GEngine && GEngine->XRSystem.IsValid())
	{
		GEngine->XRSystem->SetTrackingOrigin(EHMDTrackingOrigin::Stage);
	}

	ApplyOfficeVRSpectatorTweaks();
	LockOfficeVRHeadsetOffDLSS();
}

static TAutoConsoleVariable<int32> CVarOfficeVRSpectator(
	TEXT("officevr.Spectator"),
	1,
	TEXT("0=HMD default spectator, 1=single-eye cropped to fill the PC window (~16:9)."),
	ECVF_Default);

namespace
{
FTSTicker::FDelegateHandle SpectatorRetryHandle;

enum class ESpectatorApply : uint8
{
	Applied,
	NotReady,
	NoXR,
	Disabled
};

ESpectatorApply TryApplyOfficeVRSpectator()
{
	if (CVarOfficeVRSpectator.GetValueOnGameThread() <= 0)
	{
		return ESpectatorApply::Disabled;
	}
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return ESpectatorApply::NoXR;
	}
	IHeadMountedDisplay* HMD = GEngine->XRSystem->GetHMDDevice();
	if (!HMD)
	{
		return ESpectatorApply::NotReady;
	}
	ISpectatorScreenController* Spec = HMD->GetSpectatorScreenController();
	if (!Spec)
	{
		// OpenXR creates this when the session starts, which is after BeginPlay.
		return ESpectatorApply::NotReady;
	}
	Spec->SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
	UE_LOG(LogOfficeVR, Display, TEXT("Spectator: SingleEyeCroppedToFill. The PC window is a copy of one eye, not a second DLSS render."));
	return ESpectatorApply::Applied;
}
}

void ApplyOfficeVRSpectatorTweaks()
{
	const ESpectatorApply Result = TryApplyOfficeVRSpectator();
	if (Result == ESpectatorApply::Disabled)
	{
		UE_LOG(LogOfficeVR, Display, TEXT("Spectator: left at HMD default (officevr.Spectator=0)."));
		return;
	}
	if (Result == ESpectatorApply::NoXR)
	{
		UE_LOG(LogOfficeVR, Display, TEXT("Spectator: no XR (editor Play uses the game viewport)."));
		return;
	}
	if (Result == ESpectatorApply::Applied)
	{
		if (SpectatorRetryHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(SpectatorRetryHandle);
			SpectatorRetryHandle.Reset();
		}
		return;
	}
	if (SpectatorRetryHandle.IsValid())
	{
		return;
	}

	const double StartedAt = FPlatformTime::Seconds();
	SpectatorRetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([StartedAt](float)
	{
		const ESpectatorApply Later = TryApplyOfficeVRSpectator();
		if (Later == ESpectatorApply::Applied || Later == ESpectatorApply::NoXR || Later == ESpectatorApply::Disabled)
		{
			SpectatorRetryHandle.Reset();
			return false;
		}
		if (FPlatformTime::Seconds() - StartedAt > 8.0)
		{
			UE_LOG(LogOfficeVR, Warning, TEXT("Spectator: HMD session started without a spectator controller."));
			SpectatorRetryHandle.Reset();
			return false;
		}
		return true;
	}), 0.25f);
}

void LockOfficeVRHeadsetOffDLSS()
{
	if (!GEngine || !GEngine->XRSystem.IsValid() || !GEngine->XRSystem->GetHMDDevice())
	{
		return;
	}

	// Official DLSS/Streamline hooks the scene view. This spectator window only
	// blits an eye. Enabling NGX here upscales the OpenXR swapchain Pico streams.
	const TCHAR* Off[] = {
		TEXT("r.NGX.DLSS.Enable"),
		TEXT("r.Streamline.DLSSG.Enable"),
		TEXT("r.NGX.DLSS.FrameGeneration.Enable"),
	};
	bool bAny = false;
	for (const TCHAR* Name : Off)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(0, ECVF_SetByCode);
			bAny = true;
		}
	}
	UE_LOG(LogOfficeVR, Display, TEXT("Pico path: DLSS/FG forced off (%s). Monitor stays the current eye copy."),
		bAny ? TEXT("plugin cvars present") : TEXT("no DLSS plugin cvars"));
}
