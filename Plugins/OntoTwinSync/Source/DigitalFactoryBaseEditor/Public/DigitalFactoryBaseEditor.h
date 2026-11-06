#pragma once

#include "Modules/ModuleManager.h"

class FDigitalFactoryBaseEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
