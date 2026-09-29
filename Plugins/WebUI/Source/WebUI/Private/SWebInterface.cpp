#include "SWebInterface.h"

void SWebInterface::Construct(const FArguments& InArgs)
{
	SWebBrowser::FOnBeforeBrowse Navigation;
	if (InArgs._OnBeforeNavigation.IsBound())
	{
		Navigation.BindLambda([Nav = InArgs._OnBeforeNavigation](const FString& Url, const FWebNavigationRequest& Request)
		{
			return Nav.Execute(Url, Request);
		});
	}

	ChildSlot
	[
		SAssignNew(Inner, SWebBrowser)
		.InitialURL(InArgs._InitialURL)
		.ShowControls(false)
		.ShowAddressBar(false)
		.SupportsTransparency(true)
		.BackgroundColor(InArgs._BackgroundColor)
		.BrowserFrameRate(InArgs._FrameRate)
		.OnLoadStarted(InArgs._OnLoadStarted)
		.OnLoadCompleted(InArgs._OnLoadCompleted)
		.OnLoadError(InArgs._OnLoadError)
		.OnUrlChanged(InArgs._OnUrlChanged)
		.OnBeforePopup(InArgs._OnBeforePopup)
		.OnBeforeNavigation(Navigation)
	];
}

void SWebInterface::LoadURL(const FString& Url)
{
	if (Inner.IsValid())
	{
		Inner->LoadURL(Url);
	}
}

void SWebInterface::Reload()
{
	if (Inner.IsValid())
	{
		Inner->Reload();
	}
}

void SWebInterface::ExecuteJavascript(const FString& Script)
{
	if (Inner.IsValid())
	{
		Inner->ExecuteJavascript(Script);
	}
}

FString SWebInterface::GetUrl() const
{
	return Inner.IsValid() ? Inner->GetUrl() : FString();
}

void SWebInterface::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	if (Inner.IsValid())
	{
		Inner->BindUObject(Name, Object, bIsPermanent);
	}
}

void SWebInterface::UnbindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	if (Inner.IsValid())
	{
		Inner->UnbindUObject(Name, Object, bIsPermanent);
	}
}
