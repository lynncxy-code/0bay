#pragma once

#include "CoreMinimal.h"
#include "SWebBrowser.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FWebNavigationRequest;

class WEBUI_API SWebInterface : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnBeforeBrowse, const FString& /*Url*/, const FWebNavigationRequest& /*Request*/);

	SLATE_BEGIN_ARGS(SWebInterface)
		: _InitialURL(TEXT("about:blank"))
		, _FrameRate(60)
		, _BackgroundColor(FColor::Transparent)
	{
	}
		SLATE_ARGUMENT(FString, InitialURL)
		SLATE_ARGUMENT(int32, FrameRate)
		SLATE_ARGUMENT(FColor, BackgroundColor)
		SLATE_EVENT(FSimpleDelegate, OnLoadStarted)
		SLATE_EVENT(FSimpleDelegate, OnLoadCompleted)
		SLATE_EVENT(FSimpleDelegate, OnLoadError)
		SLATE_EVENT(FOnTextChanged, OnUrlChanged)
		SLATE_EVENT(FOnBeforePopupDelegate, OnBeforePopup)
		SLATE_EVENT(FOnBeforeBrowse, OnBeforeNavigation)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void LoadURL(const FString& Url);
	void Reload();
	void ExecuteJavascript(const FString& Script);
	FString GetUrl() const;
	void BindUObject(const FString& Name, UObject* Object, bool bIsPermanent);
	void UnbindUObject(const FString& Name, UObject* Object, bool bIsPermanent);

private:
	TSharedPtr<SWebBrowser> Inner;
};
