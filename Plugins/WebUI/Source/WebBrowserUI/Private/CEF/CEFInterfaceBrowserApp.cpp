// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
#include "CEF/CEFInterfaceBrowserApp.h"
#include "HAL/IConsoleManager.h"

#if WITH_CEF3
#include "WebInterfaceBrowserLog.h"

//#define DEBUG_CEFMESSAGELOOP_FRAMERATE 1 // uncomment this to have debug spew about the FPS we call the CefDoMessageLoopWork function

DEFINE_LOG_CATEGORY(LogCEFInterfaceBrowser);
/*
static bool bCEFGPUAcceleration = true;
static FAutoConsoleVariableRef CVarCEFGPUAcceleration(
	TEXT("r.CEFGPUAcceleration"),
	bCEFGPUAcceleration,
	TEXT("Enables GPU acceleration in CEF\n"),
	ECVF_Default);
*/
FCEFInterfaceBrowserApp::FCEFInterfaceBrowserApp()
	: MessagePumpCountdown(0)
{
}

void FCEFInterfaceBrowserApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> CommandLine)
{
}

void FCEFInterfaceBrowserApp::OnBeforeCommandLineProcessing(const CefString& ProcessType, CefRefPtr< CefCommandLine > CommandLine)
{
	CommandLine->AppendSwitch("enable-gpu");
	CommandLine->AppendSwitch("enable-gpu-compositing");
  //CommandLine->AppendSwitch("enable-media-stream");

#if PLATFORM_LINUX
	CommandLine->AppendSwitchWithValue("ozone-platform", "headless");
	CommandLine->AppendSwitchWithValue("use-gl", "angle");
	CommandLine->AppendSwitchWithValue("use-angle", "vulkan");
	CommandLine->AppendSwitch("use-vulkan");
#endif

	CommandLine->AppendSwitch("enable-begin-frame-scheduling");
	CommandLine->AppendSwitch("disable-pinch"); // the web pages we have don't expect zoom to work right now so disable touchpad pinch zoom
	CommandLine->AppendSwitch("disable-gpu-shader-disk-cache"); // Don't create a "GPUCache" directory when cache-path is unspecified.
#if PLATFORM_MAC
	CommandLine->AppendSwitch("use-mock-keychain"); // Disable the toolchain prompt on macOS.
#endif
}

#if CEF_VERSION_MAJOR >= 124
void FCEFInterfaceBrowserApp::OnScheduleMessagePumpWork(int64_t delay_ms)
#else
void FCEFInterfaceBrowserApp::OnScheduleMessagePumpWork(int64 delay_ms)
#endif
{
	FScopeLock Lock(&MessagePumpCountdownCS);

	// As per CEF documentation, if delay_ms is <= 0, then the call to CefDoMessageLoopWork should happen reasonably soon.  If delay_ms is > 0, then the call
	//  to CefDoMessageLoopWork should be scheduled to happen after the specified delay and any currently pending scheduled call should be canceled.
	if(delay_ms < 0)
	{
		delay_ms = 0;
	}
	MessagePumpCountdown = delay_ms;
}

bool FCEFInterfaceBrowserApp::TickMessagePump(float DeltaTime, bool bForce)
{
	bool bPump = false;
	{
		FScopeLock Lock(&MessagePumpCountdownCS);
		
		// count down in order to call message pump
		if (MessagePumpCountdown >= 0)
		{
			MessagePumpCountdown -= (DeltaTime * 1000);
			if (MessagePumpCountdown <= 0)
			{
				bPump = true;
			}
			
			if (bPump || bForce)
			{
				// -1 indicates that no countdown is currently happening
				MessagePumpCountdown = -1;
			}
		}
	}
	
#ifdef  DEBUG_CEFMESSAGELOOP_FRAMERATE
	static float SecondsFrameRate = 0;
	static int NumFrames = 0;
	SecondsFrameRate += DeltaTime;
#endif
	if (bPump || bForce)
	{
#ifdef DEBUG_CEFMESSAGELOOP_FRAMERATE
		++NumFrames;
		if (NumFrames % 100 == 0 || SecondsFrameRate > 5.0f)
		{
			UE_LOG(LogWebBrowser, Error, TEXT("CefDoMessageLoopWork call Frame Rate %0.2f"), NumFrames / SecondsFrameRate);
			SecondsFrameRate = 0;
			NumFrames = 0;
		}
#endif

		CefDoMessageLoopWork();
		return true;
	}
	return false;
}

#if CEF_VERSION_MAJOR >= 120
bool FCEFInterfaceBrowserApp::OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> CommandLine, const CefString& CurrentDirectory)
{
	// nothing to do, except report the relaunch as handled to prevent the default handler from opening a new browser window
	return true;
}
#endif

#endif
