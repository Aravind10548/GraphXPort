#pragma once

#include "Modules/ModuleManager.h"

class FSpawnTabArgs;
class SDockTab;

class GRAPHXPORTMERGE_API FGraphXPortMergeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** True while the merge tab may hold Slate references to loaded revision assets. */
    static bool IsMergeTabOpen();

private:
    TSharedRef<SDockTab> SpawnMergeTab(const FSpawnTabArgs& Args);
};
