#include "GraphXPortConflictToolset.h"
#include "GraphXPortConflictReader.h"
#include "UnrealMCPModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace GraphXPortMergeWorkflow
{
using FObject = TSharedPtr<FJsonObject>;
struct FSession
{
    FString Path, Id, PlanId, Patch;
    FObject Baseline, Receipt;
    double LastRead = FPlatformTime::Seconds();
    bool bApplied = false, bSave = false, bStage = false;
};
TMap<FString, TSharedPtr<FSession>> Sessions;
FObject Decode(const FString& Text)
{
    FObject Out;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
    return Out;
}
FString Encode(const FObject& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}
FString Fail(const FString& Error)
{
    auto Out = MakeShared<FJsonObject>(); Out->SetBoolField(TEXT("ok"), false); Out->SetStringField(TEXT("error"), Error); return Encode(Out);
}
FObject Native(const FString& Name, const FObject& Params)
{
    auto Envelope = Decode(FUnrealMCPModule::ExecuteToolJson(Name, Encode(Params)));
    if (!Envelope) return nullptr;
    const TSharedPtr<FJsonObject>* Result = nullptr;
    if (Envelope->TryGetObjectField(TEXT("result"), Result)) return *Result;
    return Envelope;
}
TSharedPtr<FSession> Find(const FString& Id)
{
    const double Now = FPlatformTime::Seconds();
    for (auto It = Sessions.CreateIterator(); It; ++It) if (Now - It.Value()->LastRead > 600) It.RemoveCurrent();
    auto* Found = Sessions.Find(Id);
    if (!Found) return nullptr;
    (*Found)->LastRead = Now; return *Found;
}
bool StagesMatch(const FObject& A, const FObject& B)
{
    if (!A || !B || !A->GetBoolField(TEXT("ok")) || !B->GetBoolField(TEXT("ok"))) return false;
    for (const TCHAR* Key : {TEXT("stage1"), TEXT("stage2"), TEXT("stage3")})
        if (A->GetStringField(Key).IsEmpty() || A->GetStringField(Key) != B->GetStringField(Key)) return false;
    return true;
}
bool Unchanged(const FSession& Session)
{
    auto State = Decode(FGraphXPortConflictReader::WorkingState(Session.Path));
    return StagesMatch(Session.Baseline, State) && !State->GetBoolField(TEXT("dirty"))
        && State->GetStringField(TEXT("diskHash")) == Session.Baseline->GetStringField(TEXT("diskHash"));
}
}

FString UGraphXPortConflictToolset::OpenBlueprintMerge(FString Path)
{
    using namespace GraphXPortMergeWorkflow;
    if (!IsInGameThread()) return Fail(TEXT("Requires editor game thread."));
    Find(FString()); // Evict expired sessions.
    if (Sessions.Num() >= 2) return Fail(TEXT("Two merge sessions are open; close one before opening another."));
    auto State = Decode(FGraphXPortConflictReader::WorkingState(Path));
    if (!State || !State->GetBoolField(TEXT("ok"))) return State ? Encode(State) : Fail(TEXT("Unable to inspect working state."));
    if (State->GetBoolField(TEXT("dirty"))) return Fail(TEXT("Working asset has unsaved edits. Review/save them before opening a merge session."));
    for (const TCHAR* Key : {TEXT("stage1"), TEXT("stage2"), TEXT("stage3")})
        if (State->GetStringField(Key).IsEmpty()) return Fail(TEXT("Requires three present unmerged stages. Add/delete conflicts need manual review."));
    auto Outgoing = Decode(FGraphXPortConflictReader::Compare(Path, TEXT("base"), TEXT("current"), FString(), 0, 200));
    auto Incoming = Decode(FGraphXPortConflictReader::Compare(Path, TEXT("base"), TEXT("incoming"), FString(), 0, 200));
    if (!Outgoing || !Incoming || !Outgoing->GetBoolField(TEXT("ok")) || !Incoming->GetBoolField(TEXT("ok")))
        return Fail(TEXT("Could not compare all three revisions."));
    if (Outgoing->GetBoolField(TEXT("hasMore")) || Incoming->GetBoolField(TEXT("hasMore")))
        return Fail(TEXT("More than 200 graph differences on a side; use paged revision tools for this asset."));
    auto Session = MakeShared<FSession>(); Session->Id = FGuid::NewGuid().ToString(); Session->Path = Path; Session->Baseline = State;
    if (!Unchanged(*Session)) return Fail(TEXT("Working state or Git stages changed during open; retry."));
    Sessions.Add(Session->Id, Session);
    auto Out = MakeShared<FJsonObject>(); Out->SetBoolField(TEXT("ok"), true); Out->SetStringField(TEXT("sessionId"), Session->Id);
    Out->SetObjectField(TEXT("baseline"), State); Out->SetObjectField(TEXT("outgoing"), Outgoing); Out->SetObjectField(TEXT("incoming"), Incoming);
    auto Touched = [](const FObject& Diff)
    {
        TSet<FString> Keys;
        for (const auto& Value : Diff->GetArrayField(TEXT("rows")))
        {
            auto Row = Value->AsObject();
            if (Row->HasField(TEXT("diffType")) && Row->GetIntegerField(TEXT("diffType")) == 17) continue; // Layout alone is not a semantic overlap.
            for (const TCHAR* Field : {TEXT("beforeNode"), TEXT("afterNode"), TEXT("beforePin"), TEXT("afterPin")})
            {
                const TSharedPtr<FJsonObject>* Item = nullptr; FString GuidText; FGuid Guid;
                if (Row->TryGetObjectField(Field, Item)
                    && ((*Item)->TryGetStringField(TEXT("guid"), GuidText) || (*Item)->TryGetStringField(TEXT("nodeGuid"), GuidText))
                    && FGuid::Parse(GuidText, Guid)) Keys.Add(Row->GetStringField(TEXT("graph")) + TEXT("|") + Guid.ToString());
            }
        }
        return Keys;
    };
    const auto CurrentKeys = Touched(Outgoing), IncomingKeys = Touched(Incoming);
    TArray<FString> Overlaps;
    for (const FString& Key : CurrentKeys) if (IncomingKeys.Contains(Key)) Overlaps.Add(Key);
    Overlaps.Sort(); TArray<TSharedPtr<FJsonValue>> OverlapRows;
    for (const FString& Key : Overlaps) OverlapRows.Add(MakeShared<FJsonValueString>(Key));
    Out->SetArrayField(TEXT("overlapCandidates"), OverlapRows);

    Out->SetStringField(TEXT("scope"), TEXT("Graph evidence only; no automatic semantic conflict resolution or coverage of defaults/components/member declarations. Preview requires an explicit graph patch."));
    return Encode(Out);
}

FString UGraphXPortConflictToolset::InspectMergeNodes(FString SessionId, FString Side, FString Graph, FString NodeGuidsJson)
{
    using namespace GraphXPortMergeWorkflow;
    auto Session = Find(SessionId);
    if (!Session || !Unchanged(*Session)) return Fail(TEXT("Missing/expired/stale session; reopen review."));
    if (Side != TEXT("base") && Side != TEXT("current") && Side != TEXT("incoming")) return Fail(TEXT("Side must be base/current/incoming."));
    TArray<TSharedPtr<FJsonValue>> Values;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NodeGuidsJson), Values) || Values.Num() > 100 || Values.IsEmpty() || Graph.IsEmpty())
        return Fail(TEXT("Supply graph and 1..100 node GUID strings."));
    TSet<FGuid> Guids;
    for (const auto& Value : Values) { FString Text; FGuid Id; if (!Value->TryGetString(Text) || !FGuid::Parse(Text, Id)) return Fail(TEXT("Invalid GUID.")); Guids.Add(Id); }
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Offset = 0;
    do
    {
        auto Page = Decode(FGraphXPortConflictReader::Inspect(Session->Path, Side, Graph, Offset, 200));
        if (!Page || !Page->GetBoolField(TEXT("ok"))) return Fail(TEXT("Revision inspection failed."));
        for (const auto& Value : Page->GetArrayField(TEXT("rows")))
        {
            auto Row = Value->AsObject(); FString Text; FGuid Id;
            if (Row && Row->TryGetStringField(TEXT("guid"), Text) && FGuid::Parse(Text, Id) && Guids.Contains(Id)) Rows.Add(Value);
        }
        if (!Page->GetBoolField(TEXT("hasMore"))) break;
        Offset = Page->GetIntegerField(TEXT("nextOffset"));
    } while (true);
    auto Out = MakeShared<FJsonObject>(); Out->SetBoolField(TEXT("ok"), true); Out->SetArrayField(TEXT("nodes"), Rows);
    Out->SetNumberField(TEXT("requested"), Guids.Num()); Out->SetNumberField(TEXT("returned"), Rows.Num());
    return Encode(Out);
}

FString UGraphXPortConflictToolset::PreviewMergeResolution(FString SessionId, FString PatchJson, FString DetailLevel)
{
    using namespace GraphXPortMergeWorkflow;
    auto Session = Find(SessionId);
    if (!Session || Session->bApplied || !Unchanged(*Session)) return Fail(TEXT("Missing/expired/applied/stale session; reopen review."));
    if (DetailLevel != TEXT("summary") && DetailLevel != TEXT("full")) return Fail(TEXT("DetailLevel must be summary or full."));
    if (PatchJson.Len() > 131072) return Fail(TEXT("Patch exceeds 128K characters."));
    auto Patch = Decode(PatchJson);
    if (!Patch) return Fail(TEXT("Invalid patch JSON."));
    FString Graph; Patch->TryGetStringField(TEXT("graphName"), Graph);
    if (Graph.IsEmpty()) return Fail(TEXT("Patch requires graphName."));
    const FString ObjectPath = Session->Baseline->GetStringField(TEXT("objectPath"));
    auto Query = MakeShared<FJsonObject>(); Query->SetStringField(TEXT("objectPath"), ObjectPath); Query->SetStringField(TEXT("kind"), TEXT("graph"));
    Query->SetStringField(TEXT("query"), Graph); Query->SetNumberField(TEXT("limit"), 100);
    auto Overview = Native(TEXT("GetBlueprintOverview"), Query);
    if (!Overview || !Overview->HasField(TEXT("members"))) return Fail(TEXT("Could not obtain live graph revision."));
    FString Revision;
    for (const auto& Value : Overview->GetArrayField(TEXT("members")))
        if (Value->AsObject()->GetStringField(TEXT("name")) == Graph) Revision = Value->AsObject()->GetStringField(TEXT("graphRevision"));
    if (Revision.IsEmpty()) return Fail(TEXT("Graph not found."));
    const FString PlanId = FGuid::NewGuid().ToString();
    Patch->RemoveField(TEXT("graphGuid"));
    Patch->SetStringField(TEXT("objectPath"), ObjectPath); Patch->SetStringField(TEXT("expectedGraphRevision"), Revision);
    Patch->SetStringField(TEXT("patchId"), PlanId); Patch->SetStringField(TEXT("operationId"), PlanId + TEXT(":preview"));
    Patch->SetStringField(TEXT("detailLevel"), DetailLevel); Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetBoolField(TEXT("compileAfterEdit"), true); Patch->SetBoolField(TEXT("saveAfterEdit"), false);
    auto Result = Native(TEXT("ApplyBlueprintGraphPatch"), Patch);
    if (!Result || !Result->HasField(TEXT("success")) || !Result->GetBoolField(TEXT("success"))) return Result ? Encode(Result) : Fail(TEXT("Native preview failed."));
    Session->PlanId = PlanId; Session->Patch = Encode(Patch);
    Result->SetStringField(TEXT("planId"), PlanId); Result->SetStringField(TEXT("sessionId"), SessionId);
    return Encode(Result);
}

FString UGraphXPortConflictToolset::ApplyMergeResolution(FString SessionId, FString PlanId, bool Save, bool Stage)
{
    using namespace GraphXPortMergeWorkflow;
    auto Session = Find(SessionId);
    if (!Session || Session->PlanId != PlanId) return Fail(TEXT("Missing/expired session or unknown plan. Preview first."));
    if (Session->bApplied)
        return Session->bSave == Save && Session->bStage == Stage ? Encode(Session->Receipt) : Fail(TEXT("Plan already applied with different Save/Stage flags."));
    if (Stage && !Save) return Fail(TEXT("Stage requires explicit Save=true."));
    if (!Unchanged(*Session)) return Fail(TEXT("Working copy, dirty state or Git stages changed; reopen review."));
    auto Patch = Decode(Session->Patch);
    Patch->SetStringField(TEXT("detailLevel"), TEXT("summary"));
    Patch->SetBoolField(TEXT("dryRun"), false); Patch->SetBoolField(TEXT("saveAfterEdit"), Save); Patch->SetStringField(TEXT("operationId"), PlanId + TEXT(":apply"));
    auto Result = Native(TEXT("ApplyBlueprintGraphPatch"), Patch);
    if (!Result || !Result->HasField(TEXT("success")) || !Result->GetBoolField(TEXT("success"))) return Result ? Encode(Result) : Fail(TEXT("Native apply failed."));
    bool bStaged = false;
    if (Stage && Result->GetBoolField(TEXT("compileSucceeded")) && Result->GetBoolField(TEXT("saved")))
    {
        auto State = Decode(FGraphXPortConflictReader::WorkingState(Session->Path));
        if (StagesMatch(Session->Baseline, State) && !State->GetBoolField(TEXT("dirty")))
            bStaged = FGraphXPortConflictReader::StageResolvedFile(Session->Path, State->GetStringField(TEXT("diskHash")));
    }
    Result->SetBoolField(TEXT("workflowSucceeded"), !Stage || bStaged);
    if (Stage && !bStaged) Result->SetStringField(TEXT("stageError"), TEXT("Patch applied, but staging was refused or failed. Inspect saved/compile state; Git conflict remains unresolved."));
    Result->SetBoolField(TEXT("staged"), bStaged); Result->SetBoolField(TEXT("stageRequested"), Stage);
    Result->SetStringField(TEXT("sessionId"), SessionId); Result->SetStringField(TEXT("planId"), PlanId);
    Session->bApplied = true; Session->bSave = Save; Session->bStage = Stage; Session->Receipt = Result;
    return Encode(Result);
}
void UGraphXPortConflictToolset::CloseBlueprintMerge(FString SessionId)
{
    GraphXPortMergeWorkflow::Sessions.Remove(SessionId);
    if (GraphXPortMergeWorkflow::Sessions.IsEmpty()) FGraphXPortConflictReader::ClearRevisionCache();
}
