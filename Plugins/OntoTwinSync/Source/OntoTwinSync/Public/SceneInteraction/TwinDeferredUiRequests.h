#pragma once

#include "CoreMinimal.h"

enum class ETwinUiRequest : uint8
{
    ToggleHud, OpenHudFromWeb, ToggleRoaming, WebBack, WebClose
};

// Slate callbacks only enqueue. World Tick consumes on a strictly later frame.
// OpenHud wins over ToggleHud from another input path for the same key edge.
struct FTwinDeferredUiRequests
{
    struct FRequest { ETwinUiRequest Action; uint64 Frame; };
    TArray<FRequest> Pending;
    uint64 LastFrames[4] = { MAX_uint64, MAX_uint64, MAX_uint64, MAX_uint64 };

    static int32 Group(ETwinUiRequest Action)
    {
        return Action == ETwinUiRequest::OpenHudFromWeb ? 0
            : Action == ETwinUiRequest::ToggleHud ? 0 : static_cast<int32>(Action) - 1;
    }

    void Enqueue(ETwinUiRequest Action, uint64 Frame)
    {
        const int32 Index = Group(Action);
        for (FRequest& Request : Pending)
        {
            if (Group(Request.Action) == Index)
            {
                if (Action == ETwinUiRequest::OpenHudFromWeb) Request.Action = Action;
                LastFrames[Index] = Frame;
                return;
            }
        }
        if (LastFrames[Index] == Frame) return;
        LastFrames[Index] = Frame;
        Pending.Add({Action, Frame});
    }

    TArray<FRequest> TakeReady(uint64 Frame)
    {
        TArray<FRequest> Ready;
        for (int32 Index = 0; Index < Pending.Num();)
        {
            if (Pending[Index].Frame < Frame)
            {
                Ready.Add(Pending[Index]);
                LastFrames[Group(Pending[Index].Action)] = Frame;
                Pending.RemoveAt(Index);
            }
            else ++Index;
        }
        return Ready;
    }
};
