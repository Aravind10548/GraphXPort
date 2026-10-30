#include "GraphXPortConflictToolset.h"
#include "GraphXPortConflictReader.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
class FGraphXPortConflictToolsetModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UToolsetRegistry::RegisterToolsetClass(UGraphXPortConflictToolset::StaticClass());
    }
    virtual void ShutdownModule() override
    {
        if (UObjectInitialized() && !IsEngineExitRequested())
            UToolsetRegistry::UnregisterToolsetClass(UGraphXPortConflictToolset::StaticClass());
    }
};
IMPLEMENT_MODULE(FGraphXPortConflictToolsetModule, GraphXPortConflictToolset)
FString UGraphXPortConflictToolset::ListConflicts() { return FGraphXPortConflictReader::ListConflicts(); }
FString UGraphXPortConflictToolset::InspectRevision(FString Path, FString Revision, FString Graph, int32 Offset, int32 Limit)
{ return FGraphXPortConflictReader::Inspect(Path, Revision, Graph, Offset, Limit); }
FString UGraphXPortConflictToolset::CompareRevisions(FString Path, FString Before, FString After, FString Graph, int32 Offset, int32 Limit)
{ return FGraphXPortConflictReader::Compare(Path, Before, After, Graph, Offset, Limit); }
