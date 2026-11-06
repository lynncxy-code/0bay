#pragma once
#include "CoreMinimal.h"

namespace OntoTwinCommunicationColours
{
inline FLinearColor ForSeverity(int32 Severity)
{
    if (Severity >= 2) return FLinearColor(1.f, .005f, .005f);
    if (Severity == 1) return FLinearColor(1.f, .22f, .005f);
    return FLinearColor(.01f, 1.f, .08f);
}
}
