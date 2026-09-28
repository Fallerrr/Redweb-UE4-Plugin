#include "Modules/ModuleManager.h"

class FRedwebBPModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FRedwebBPModule, RedwebBP)
