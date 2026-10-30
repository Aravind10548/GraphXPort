#include "GraphXPortConflictReader.h"
#include "GraphXPortGitStatus.h"
#include "GraphXPortGitBlob.h"
#include "GraphXPortTempPackage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "GraphDiffControl.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"

namespace GraphXPortConflictReaderPrivate
{
using FObject = TSharedRef<FJsonObject>;
using FRows = TArray<TSharedPtr<FJsonValue>>;
struct FCachedRevision
{
    FGraphXPortLoadedAsset Asset;
    TStrongObjectPtr<UPackage> KeepAlive;
    TStrongObjectPtr<UObject> KeepAsset;
    TStrongObjectPtr<UBlueprint> KeepBlueprint;
    double LastRead = 0;
    explicit FCachedRevision(const FGraphXPortLoadedAsset& In) : Asset(In), KeepAlive(In.Package), KeepAsset(In.Asset), KeepBlueprint(In.Blueprint), LastRead(FPlatformTime::Seconds()) {}
};
TMap<FString, TSharedPtr<FCachedRevision>> RevisionCache;
void TrimCache()
{
    const double Now = FPlatformTime::Seconds();
    for (auto It = RevisionCache.CreateIterator(); It; ++It) if (Now - It.Value()->LastRead > 300) It.RemoveCurrent();
    while (RevisionCache.Num() >= 6)
    {
        FString Oldest; double Time = TNumericLimits<double>::Max();
        for (const auto& Pair : RevisionCache) if (Pair.Value->LastRead < Time) { Oldest = Pair.Key; Time = Pair.Value->LastRead; }
        RevisionCache.Remove(Oldest);
    }
}
FString Encode(FObject Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    return Text;
}
FString Failure(const FString& Message)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("ok"), false);
    Out->SetStringField(TEXT("error"), Message);
    return Encode(Out);
}
bool Root(FString& Repo)
{
    return IsInGameThread() && FGraphXPortGitStatus::DetectAvailability(Repo) == EGraphXPortGitAvailability::Available;
}
bool ValidPath(const FString& Repo, const FString& Path)
{
    // Restrict the arguments passed to GraphXPort's existing quoted git invocation.
    if (Path.IsEmpty() || !FPaths::IsRelative(Path) || Path.Contains(TEXT("\"")) || Path.Contains(TEXT(":")) || Path.Contains(TEXT("\\"))) return false;
    for (TCHAR C : Path) if (C < 32) return false;
    FString Absolute = FPaths::ConvertRelativePathToFull(Repo / Path);
    FPaths::CollapseRelativeDirectories(Absolute);
    FString NormalizedRepo = Repo; FPaths::NormalizeDirectoryName(NormalizedRepo);
    return Absolute.StartsWith(NormalizedRepo + TEXT("/")) && (Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap")));
}
FString Ref(const FString& Revision)
{
    if (Revision == TEXT("base")) return TEXT(":1");
    if (Revision == TEXT("current")) return TEXT(":2");
    if (Revision == TEXT("incoming")) return TEXT(":3");
    if (Revision == TEXT("HEAD")) return Revision;
    if (Revision.Len() == 40 || Revision.Len() == 64)
    {
        for (TCHAR C : Revision) if (!FChar::IsHexDigit(C)) return FString();
        return Revision;
    }
    return FString();
}
bool Load(const FString& Repo, const FString& Path, const FString& Revision, FGraphXPortLoadedAsset& Asset, FString& Id, FString& Message)
{
    const FString Spec = Ref(Revision);
    if (Spec.IsEmpty()) { Message = TEXT("Use base, current, incoming, HEAD, or a full commit hash."); return false; }
    if (!FGraphXPortGitBlob::GetBlobId(Repo, Spec, Path, Id)) { Message = TEXT("Revision is absent; add/delete conflicts can have missing stages."); return false; }
    const FString CacheKey = Repo + TEXT("|") + Path + TEXT("|") + Id;
    if (auto* Cached = RevisionCache.Find(CacheKey))
    {
        if (FPlatformTime::Seconds() - (*Cached)->LastRead <= 300)
        { Asset = (*Cached)->Asset; (*Cached)->LastRead = FPlatformTime::Seconds(); return true; }
        RevisionCache.Remove(CacheKey);
    }
    // Verify the identity again after extraction to detect index changes during loading.
    if (!FGraphXPortTempPackage::LoadBlobAsAsset(Repo, Spec, Path, Asset, Message)) return false;
    FString AfterId;
    if (!FGraphXPortGitBlob::GetBlobId(Repo, Spec, Path, AfterId) || AfterId != Id) { Message = TEXT("Revision changed while loading; retry."); return false; }
    if (!Asset.Blueprint) { Message = TEXT("Only Blueprint and existing Level Blueprint graphs are supported."); return false; }
    TrimCache(); RevisionCache.Add(CacheKey, MakeShared<FCachedRevision>(Asset));
    return true;
}
FObject Node(UEdGraphNode* Value)
{
    auto Out = MakeShared<FJsonObject>();
    if (!Value) return Out;
    Out->SetStringField(TEXT("guid"), Value->NodeGuid.ToString());
    Out->SetStringField(TEXT("title"), Value->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Out->SetStringField(TEXT("class"), Value->GetClass()->GetPathName());
    Out->SetNumberField(TEXT("positionX"), Value->NodePosX); Out->SetNumberField(TEXT("positionY"), Value->NodePosY);
    Out->SetStringField(TEXT("comment"), Value->NodeComment);
    if (auto* Call = Cast<UK2Node_CallFunction>(Value))
    {
        Out->SetStringField(TEXT("kind"), TEXT("functionCall"));
        Out->SetStringField(TEXT("functionName"), Call->FunctionReference.GetMemberName().ToString());
        Out->SetStringField(TEXT("ownerClassPath"), GetPathNameSafe(Call->FunctionReference.GetMemberParentClass()));
    }
    if (auto* Variable = Cast<UK2Node_Variable>(Value))
    {
        Out->SetStringField(TEXT("variableName"), Variable->VariableReference.GetMemberName().ToString());
        Out->SetBoolField(TEXT("selfContext"), Variable->VariableReference.IsSelfContext());
        Out->SetStringField(TEXT("ownerClassPath"), GetPathNameSafe(Variable->VariableReference.GetMemberParentClass()));
    }
    return Out;
}
FObject Pin(UEdGraphPin* Value)
{
    auto Out = MakeShared<FJsonObject>();
    if (!Value) return Out;
    Out->SetStringField(TEXT("name"), Value->PinName.ToString());
    Out->SetStringField(TEXT("id"), Value->PinId.ToString());
    Out->SetStringField(TEXT("nodeGuid"), Value->GetOwningNode()->NodeGuid.ToString());
    Out->SetStringField(TEXT("direction"), Value->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    Out->SetStringField(TEXT("category"), Value->PinType.PinCategory.ToString());
    Out->SetStringField(TEXT("typeObjectPath"), GetPathNameSafe(Value->PinType.PinSubCategoryObject.Get()));
    Out->SetStringField(TEXT("defaultValue"), Value->DefaultValue);
    Out->SetStringField(TEXT("defaultObject"), GetPathNameSafe(Value->DefaultObject));
    Out->SetStringField(TEXT("defaultText"), Value->DefaultTextValue.ToString());
    FRows Links;
    for (const auto* Link : Value->LinkedTo) if (Link)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("nodeGuid"), Link->GetOwningNode()->NodeGuid.ToString());
        Item->SetStringField(TEXT("pinId"), Link->PinId.ToString());
        Links.Add(MakeShared<FJsonValueObject>(Item));
    }
    Out->SetArrayField(TEXT("links"), Links);
    return Out;
}
FString Page(FRows& Rows, int32 Offset, int32 Limit, FObject Out)
{
    Offset = FMath::Max(0, Offset); Limit = FMath::Clamp(Limit, 1, 200);
    FRows PageRows;
    const int32 End = Offset >= Rows.Num() ? Rows.Num() : Offset + FMath::Min(Limit, Rows.Num() - Offset);
    for (int32 I = Offset; I < End; ++I) PageRows.Add(Rows[I]);
    Out->SetBoolField(TEXT("ok"), true);
    Out->SetNumberField(TEXT("total"), Rows.Num());
    Out->SetNumberField(TEXT("nextOffset"), End);
    Out->SetBoolField(TEXT("hasMore"), End < Rows.Num());
    Out->SetArrayField(TEXT("rows"), PageRows);
    Out->SetStringField(TEXT("coverage"), TEXT("Graphs, nodes and pins only. Class defaults, components, variable declarations and non-graph properties are not compared. Graph differences are not proof of semantic conflicts."));
    return Encode(Out);
}
}

FString FGraphXPortConflictReader::ListConflicts()
{
    using namespace GraphXPortConflictReaderPrivate;
    FString Repo;
    if (!Root(Repo)) return Failure(TEXT("Requires game thread and a project Git repository."));
    TArray<FGraphXPortGitFileEntry> Files, Staged;
    if (!FGraphXPortGitStatus::ListChanges(Repo, Files, Staged)) return Failure(TEXT("Git status failed."));
    FRows Rows;
    for (const auto& File : Files) if (File.bConflicted)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), File.RepoRelativePath);
        for (const FString Side : {FString(TEXT("base")), FString(TEXT("current")), FString(TEXT("incoming"))})
        {
            FString Id;
            FGraphXPortGitBlob::GetBlobId(Repo, Ref(Side), File.RepoRelativePath, Id);
            Item->SetStringField(Side + TEXT("BlobId"), Id);
        }
        Rows.Add(MakeShared<FJsonValueObject>(Item));
    }
    auto Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("ok"), true);
    Out->SetStringField(TEXT("repository"), Repo);
    Out->SetStringField(TEXT("stageMeaning"), TEXT("base=stage1, current=stage2, incoming=stage3. During rebase these labels follow Git index stages, not branch ownership."));
    Out->SetArrayField(TEXT("conflicts"), Rows);
    return Encode(Out);
}

FString FGraphXPortConflictReader::Inspect(const FString& Path, const FString& Revision, const FString& Graph, int32 Offset, int32 Limit)
{
    using namespace GraphXPortConflictReaderPrivate;
    FString Repo, Id, Message;
    if (!Root(Repo) || !ValidPath(Repo, Path)) return Failure(TEXT("Invalid repository-relative asset path or editor context."));
    FGraphXPortLoadedAsset Asset;
    if (!Load(Repo, Path, Revision, Asset, Id, Message)) return Failure(Message);
    TStrongObjectPtr<UPackage> KeepAlive(Asset.Package);
    TArray<UEdGraph*> Graphs; Asset.Blueprint->GetAllGraphs(Graphs);
    FRows Rows;
    for (auto* G : Graphs) if (G && (Graph.IsEmpty() || G->GetName() == Graph))
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("graph"), G->GetName());
        Item->SetStringField(TEXT("graphGuid"), G->GraphGuid.ToString());
        Item->SetNumberField(TEXT("nodeCount"), G->Nodes.Num());
        Rows.Add(MakeShared<FJsonValueObject>(Item));
        // Empty selector returns graph inventory; select a graph for detailed node/pin reads.
        if (!Graph.IsEmpty()) for (UEdGraphNode* N : G->Nodes) if (N)
        {
            auto Row = Node(N); FRows Pins;
            for (auto* P : N->Pins) if (P) Pins.Add(MakeShared<FJsonValueObject>(Pin(P)));
            Row->SetArrayField(TEXT("pins"), Pins);
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    if (!Graph.IsEmpty() && Rows.IsEmpty()) return Failure(TEXT("Graph not found at this revision."));
    auto Out = MakeShared<FJsonObject>(); Out->SetStringField(TEXT("blobId"), Id);
    Out->SetStringField(TEXT("path"), Path); Out->SetStringField(TEXT("revision"), Revision);
    return Page(Rows, Offset, Limit, Out);
}

FString FGraphXPortConflictReader::Compare(const FString& Path, const FString& Before, const FString& After, const FString& Graph, int32 Offset, int32 Limit)
{
    using namespace GraphXPortConflictReaderPrivate;
    FString Repo, AId, BId, Message;
    if (!Root(Repo) || !ValidPath(Repo, Path)) return Failure(TEXT("Invalid repository-relative asset path or editor context."));
    FGraphXPortLoadedAsset A, B;
    if (!Load(Repo, Path, Before, A, AId, Message)) return Failure(Message);
    TStrongObjectPtr<UPackage> KeepA(A.Package);
    if (!Load(Repo, Path, After, B, BId, Message)) return Failure(Message);
    TStrongObjectPtr<UPackage> KeepB(B.Package);
    TArray<UEdGraph*> AG, BG; A.Blueprint->GetAllGraphs(AG); B.Blueprint->GetAllGraphs(BG);
    if (!Graph.IsEmpty() && !AG.ContainsByPredicate([&](const UEdGraph* G) { return G && G->GetName() == Graph; })
        && !BG.ContainsByPredicate([&](const UEdGraph* G) { return G && G->GetName() == Graph; })) return Failure(TEXT("Graph not found on either side."));
    TSet<UEdGraph*> Matched;
    FRows Rows;
    auto CompareGraph = [&](UEdGraph* Old, UEdGraph* New)
    {
        const FString Name = Old ? Old->GetName() : New->GetName();
        if (!Graph.IsEmpty() && Graph != Name) return;
        if (!Old || !New)
        {
            auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("graph"), Name);
            Row->SetStringField(TEXT("change"), Old ? TEXT("graphRemoved") : TEXT("graphAdded"));
            Rows.Add(MakeShared<FJsonValueObject>(Row)); return;
        }
        TArray<FDiffSingleResult> Diffs;
        FGraphDiffControl::DiffGraphs(Old, New, Diffs);
        for (const auto& D : Diffs) if (D.IsRealDifference())
        {
            auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("graph"), Name);
            Row->SetNumberField(TEXT("diffType"), static_cast<int32>(D.Diff));
            Row->SetStringField(TEXT("description"), D.DisplayString.ToString());
            Row->SetObjectField(TEXT("beforeNode"), Node(D.Node1 && D.Node1->GetGraph() == Old ? D.Node1 : D.Node2 && D.Node2->GetGraph() == Old ? D.Node2 : nullptr));
            Row->SetObjectField(TEXT("afterNode"), Node(D.Node1 && D.Node1->GetGraph() == New ? D.Node1 : D.Node2 && D.Node2->GetGraph() == New ? D.Node2 : nullptr));
            Row->SetObjectField(TEXT("beforePin"), Pin(D.Pin1));
            Row->SetObjectField(TEXT("afterPin"), Pin(D.Pin2));
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
    };
    for (auto* Old : AG) if (Old)
    {
        UEdGraph* New = nullptr;
        for (auto* Candidate : BG) if (Candidate && !Matched.Contains(Candidate) && Candidate->GraphGuid == Old->GraphGuid) { New = Candidate; break; }
        if (!New) for (auto* Candidate : BG) if (Candidate && !Matched.Contains(Candidate) && Candidate->GetName() == Old->GetName()) { New = Candidate; break; }
        if (New) Matched.Add(New);
        CompareGraph(Old, New);
    }
    for (auto* New : BG) if (New && !Matched.Contains(New)) CompareGraph(nullptr, New);
    auto Out = MakeShared<FJsonObject>(); Out->SetStringField(TEXT("beforeBlobId"), AId); Out->SetStringField(TEXT("afterBlobId"), BId);
    Out->SetStringField(TEXT("direction"), Before + TEXT(" -> ") + After);
    return Page(Rows, Offset, Limit, Out);
}

FString FGraphXPortConflictReader::WorkingState(const FString& Path)
{
    using namespace GraphXPortConflictReaderPrivate;
    FString Repo, Error, Hash; FGraphXPortLoadedAsset Asset;
    if (!Root(Repo) || !ValidPath(Repo, Path)) return Failure(TEXT("Invalid asset path or editor context."));
    if (!FGraphXPortTempPackage::ResolveRealAsset(Repo, Path, Asset, Error) || !Asset.Blueprint) return Failure(Error);
    FGraphXPortGitBlob::HashWorkingFile(Repo, Path, Hash);
    auto Out = MakeShared<FJsonObject>(); Out->SetBoolField(TEXT("ok"), true);
    Out->SetStringField(TEXT("objectPath"), Asset.Blueprint->GetPathName());
    Out->SetStringField(TEXT("diskHash"), Hash); Out->SetBoolField(TEXT("dirty"), Asset.Package->IsDirty());
    for (int32 Stage = 1; Stage <= 3; ++Stage)
    {
        FString Id; FGraphXPortGitBlob::GetBlobId(Repo, FString::Printf(TEXT(":%d"), Stage), Path, Id);
        Out->SetStringField(FString::Printf(TEXT("stage%d"), Stage), Id);
    }
    return Encode(Out);
}
bool FGraphXPortConflictReader::StageResolvedFile(const FString& Path, const FString& ExpectedDiskHash)
{
    using namespace GraphXPortConflictReaderPrivate;
    FString Repo, Hash;
    return Root(Repo) && ValidPath(Repo, Path) && !ExpectedDiskHash.IsEmpty()
        && FGraphXPortGitBlob::HashWorkingFile(Repo, Path, Hash) && Hash == ExpectedDiskHash
        && FGraphXPortGitStatus::StageFile(Repo, Path);
}
void FGraphXPortConflictReader::ClearRevisionCache()
{
    GraphXPortConflictReaderPrivate::RevisionCache.Empty();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphXPortConflictInputTest, "GraphXPort.Merge.ConflictReader.InputValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphXPortConflictInputTest::RunTest(const FString& Parameters)
{
    using namespace GraphXPortConflictReaderPrivate;
    TestTrue(TEXT("Asset path"), ValidPath(TEXT("C:/Repo"), TEXT("Content/My BP.uasset")));
    TestFalse(TEXT("Sibling escape"), ValidPath(TEXT("C:/Repo"), TEXT("../RepoOther/BP.uasset")));
    TestFalse(TEXT("Quote injection"), ValidPath(TEXT("C:/Repo"), TEXT("Content/BP\".uasset")));
    TestFalse(TEXT("Absolute path"), ValidPath(TEXT("C:/Repo"), TEXT("C:/Repo/BP.uasset")));
    TestFalse(TEXT("Control character"), ValidPath(TEXT("C:/Repo"), TEXT("Content/BP\n.uasset")));
    TestEqual(TEXT("Current stage"), Ref(TEXT("current")), FString(TEXT(":2")));
    TestTrue(TEXT("Arbitrary git argument refused"), Ref(TEXT("HEAD\" --output=x")).IsEmpty());
    TestTrue(TEXT("Full hash accepted"), !Ref(FString::ChrN(40, TEXT('a'))).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphXPortConflictPageTest, "GraphXPort.Merge.ConflictReader.Pagination", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphXPortConflictPageTest::RunTest(const FString& Parameters)
{
    using namespace GraphXPortConflictReaderPrivate;
    FRows Rows;
    for (int32 I = 0; I < 3; ++I) Rows.Add(MakeShared<FJsonValueNumber>(I));
    auto Out = MakeShared<FJsonObject>();
    Page(Rows, 1, 1, Out);
    TestEqual(TEXT("Total retains complete count"), Out->GetIntegerField(TEXT("total")), 3);
    TestEqual(TEXT("One row"), Out->GetArrayField(TEXT("rows")).Num(), 1);
    TestEqual(TEXT("Selected row"), Out->GetArrayField(TEXT("rows"))[0]->AsNumber(), 1.0);
    TestTrue(TEXT("Continuation"), Out->GetBoolField(TEXT("hasMore")));
    Page(Rows, MAX_int32, MAX_int32, Out);
    TestEqual(TEXT("Offset beyond end is empty"), Out->GetArrayField(TEXT("rows")).Num(), 0);
    TestFalse(TEXT("No continuation beyond end"), Out->GetBoolField(TEXT("hasMore")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphXPortConflictCacheTest, "GraphXPort.Merge.ConflictReader.CacheRetention", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphXPortConflictCacheTest::RunTest(const FString& Parameters)
{
    using namespace GraphXPortConflictReaderPrivate;
    UPackage* Package = CreatePackage(*(TEXT("/Temp/GraphXPortCache_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UBlueprint* Blueprint = NewObject<UBlueprint>(Package, NAME_None, RF_Transient);
    TWeakObjectPtr<UBlueprint> Weak = Blueprint;
    FGraphXPortLoadedAsset Asset; Asset.Package = Package; Asset.Asset = Blueprint; Asset.Blueprint = Blueprint;
    auto Cached = MakeShared<FCachedRevision>(Asset);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("Cached Blueprint survives GC, not just its outer package"), Weak.IsValid());
    TestEqual(TEXT("Cached Blueprint identity retained"), Cached->KeepBlueprint.Get(), Weak.Get());
    return true;
}
#endif
