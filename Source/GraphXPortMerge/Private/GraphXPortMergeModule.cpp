#include "GraphXPortMergeModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GraphXPortTempPackage.h"
#include "GraphXPortConflictReader.h"
#include "SGraphXPortMergeWindow.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "GraphXPortMerge"

namespace GraphXPortMergeTabIds
{
    static const FName MergeWindow(TEXT("GraphXPortMerge"));
}

void FGraphXPortMergeModule::StartupModule()
{
    FGraphXPortTempPackage::CleanStaleTempFiles();

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        GraphXPortMergeTabIds::MergeWindow,
        FOnSpawnTab::CreateRaw(this, &FGraphXPortMergeModule::SpawnMergeTab))
        .SetDisplayName(LOCTEXT("MergeTabTitle", "GraphXPort Merge"))
        .SetTooltipText(LOCTEXT("MergeTabTooltip", "Resolve Blueprint git conflicts side by side."))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsStructureRoot())
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Merge")));
}

void FGraphXPortMergeModule::ShutdownModule()
{
    FGraphXPortConflictReader::ClearRevisionCache();
    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GraphXPortMergeTabIds::MergeWindow);
    }
}

bool FGraphXPortMergeModule::IsMergeTabOpen()
{
    return FGlobalTabmanager::Get()->FindExistingLiveTab(GraphXPortMergeTabIds::MergeWindow).IsValid();
}

TSharedRef<SDockTab> FGraphXPortMergeModule::SpawnMergeTab(const FSpawnTabArgs& Args)
{
    TSharedRef<SGraphXPortMergeWindow> Window = SNew(SGraphXPortMergeWindow);
    TWeakPtr<SGraphXPortMergeWindow> WeakWindow = Window;

    TSharedRef<SDockTab> Tab = SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(LOCTEXT("MergeTabTitle", "GraphXPort Merge"))
        [
            Window
        ];

    // Revisions stay loaded as real assets while the window is open, which means their
    // classes and structs appear in class/struct pickers. Closing the tab clears them.
    Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
        [WeakWindow](TSharedRef<SDockTab>)
        {
            if (TSharedPtr<SGraphXPortMergeWindow> Pinned = WeakWindow.Pin())
            {
                Pinned->ReleaseRevisions();
            }
            FGraphXPortTempPackage::PurgeLoadedRevisions();
        }));

    return Tab;
}

IMPLEMENT_MODULE(FGraphXPortMergeModule, GraphXPortMerge)

#undef LOCTEXT_NAMESPACE
