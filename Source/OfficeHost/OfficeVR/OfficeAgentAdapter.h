#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_TwoParams(FOfficeAgentHttpDone, bool /*bOk*/, FString /*JsonOrError*/);
DECLARE_DELEGATE_TwoParams(FOfficeAgentBinaryDone, bool /*bOk*/, TArray<uint8> /*Bytes*/);

class FOfficeAgentAdapter
{
public:
	static FOfficeAgentAdapter& Get();

	FString GetSidecarUrl() const;
	void BeginTalk(const FString& AgentId, FOfficeAgentHttpDone OnDone);
	void PushText(const FString& Text, FOfficeAgentHttpDone OnDone);
	void StartListen(FOfficeAgentHttpDone OnDone);
	void StopListen(FOfficeAgentHttpDone OnDone);
	void EndTalk(FOfficeAgentHttpDone OnDone);
	void FetchTtsWav(FOfficeAgentBinaryDone OnDone);

private:
	void PostJson(const FString& Path, const FString& Body, FOfficeAgentHttpDone OnDone) const;
};
