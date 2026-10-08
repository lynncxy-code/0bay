#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"

class FMetaverseClientModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void OnPostWorldInitialization(UWorld* World, const UWorld::InitializationValues IVS);
    FDelegateHandle WorldInitHandle;
};
