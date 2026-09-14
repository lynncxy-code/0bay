#include "RuntimeEditor/TwinRuntimeEditorCameraPawn.h"

ATwinRuntimeEditorCameraPawn::ATwinRuntimeEditorCameraPawn()
{
    AutoPossessPlayer = EAutoReceiveInput::Disabled;
    SetActorEnableCollision(false);
}
