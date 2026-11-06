#pragma once

#include "CoreMinimal.h"

/** Transient, session-only feedback for a minimap teleport target. */
enum class ETwinMinimapTeleportFeedback : uint8
{
    Hidden,
    Valid,
    Adjusted,
    Invalid,
    Success,
};
