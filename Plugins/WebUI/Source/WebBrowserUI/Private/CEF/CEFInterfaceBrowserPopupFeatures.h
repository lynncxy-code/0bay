// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_CEF3

#include "IWebInterfaceBrowserPopupFeatures.h"

#include "CEFInterfaceLibCefIncludes.h"

#endif

class IWebInterfaceBrowserPopupFeatures;

#if WITH_CEF3

class FCEFInterfaceBrowserPopupFeatures
	: public IWebInterfaceBrowserPopupFeatures
{
public:
	FCEFInterfaceBrowserPopupFeatures();
	FCEFInterfaceBrowserPopupFeatures(const CefPopupFeatures& PopupFeatures);
	virtual ~FCEFInterfaceBrowserPopupFeatures();
	void SetResizable(const bool bResize);

	// IWebBrowserPopupFeatures Interface

	virtual int GetX() const override;
	virtual bool IsXSet() const override;
	virtual int GetY() const override;
	virtual bool IsYSet() const override;
	virtual int GetWidth() const override;
	virtual bool IsWidthSet() const override;
	virtual int GetHeight() const override;
	virtual bool IsHeightSet() const override;
	virtual bool IsMenuBarVisible() const override;
	virtual bool IsStatusBarVisible() const override;
	virtual bool IsToolBarVisible() const override;
	virtual bool IsLocationBarVisible() const override;
	virtual bool IsScrollbarsVisible() const override;
	virtual bool IsResizable() const override;
	virtual bool IsFullscreen() const override;
	virtual bool IsDialog() const override;
	virtual TArray<FString> GetAdditionalFeatures() const override;

private:

	int X;
	bool bXSet;
	int Y;
	bool bYSet;
	int Width;
	bool bWidthSet;
	int Height;
	bool bHeightSet;

	bool bMenuBarVisible;
	bool bStatusBarVisible;
	bool bToolBarVisible;
	bool bLocationBarVisible;
	bool bScrollbarsVisible;
	bool bResizable;

	bool bIsFullscreen;
	bool bIsDialog;
};

#endif
