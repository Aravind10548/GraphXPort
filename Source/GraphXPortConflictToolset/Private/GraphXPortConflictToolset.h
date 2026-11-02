#pragma once
#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "GraphXPortConflictToolset.generated.h"

/** Git conflict review and guarded patch resolution. Results are bounded JSON strings. */
UCLASS(BlueprintType, Hidden)
class UGraphXPortConflictToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** List unmerged paths with stage 1/2/3 blob identities. Never stages or saves. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString ListConflicts();

    /** Read graph inventory (Graph empty) or nodes/pins of a named graph. Revision accepts base/current/incoming/HEAD/full commit hash. Path is repository-relative. Offset starts at zero; Limit is 1..200. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString InspectRevision(FString Path, FString Revision, FString Graph, int32 Offset = 0, int32 Limit = 50);

    /** Compare graph changes from Before to After. Use base->current for outgoing and base->incoming for incoming. Revisions accept base/current/incoming/HEAD/full commit hash. Empty Graph includes all graphs. Graph overlap is not proof of semantic conflict. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString CompareRevisions(FString Path, FString Before, FString After, FString Graph, int32 Offset = 0, int32 Limit = 50);

    /** Compare Blueprint class-default values between revisions, with changed property paths and before/after values. Revisions accept base/current/incoming/HEAD/full commit hash. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString CompareBlueprintDefaults(FString Path, FString Before, FString After, int32 Offset = 0, int32 Limit = 50);

    /** Open a bounded merge review. Returns outgoing/incoming graph and Blueprint class-default changes, records stage/disk identities and dirty state. Does not edit. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString OpenBlueprintMerge(FString Path);

    /** Explicitly replace the working asset with current or incoming as a base. Requires Confirm=true, a clean unchanged three-way session, and the GraphXPort Merge tab closed. Nothing is staged. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString StartFromRevision(FString SessionId, FString Side, bool Confirm = false);

    /** Read only the requested node GUIDs, including pins and owner/member identities, from an unchanged session revision. NodeGuidsJson is a JSON string array, at most 100 GUIDs. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString InspectMergeNodes(FString SessionId, FString Side, FString Graph, FString NodeGuidsJson);

    /** Preflight an exact ApplyBlueprintGraphPatch JSON plan against the session. Captures graph revision and stores the plan; returns PlanId and compact evidence. No edit/save/stage. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString PreviewMergeResolution(FString SessionId, FString PatchJson, FString DetailLevel = TEXT("summary"));

    /** Apply the stored plan with stale-state guards and native transactional compile/rollback. Save and Stage are explicit; Stage requires Save. Retry same PlanId/flags returns the receipt. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static FString ApplyMergeResolution(FString SessionId, FString PlanId, bool Save = false, bool Stage = false);

    /** Release session evidence; never unload packages held by the merge UI. Sessions also expire after ten minutes. */
    UFUNCTION(meta=(AICallable), Category="GraphXPort Conflicts")
    static void CloseBlueprintMerge(FString SessionId);
};
