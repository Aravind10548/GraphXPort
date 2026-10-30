#pragma once
#include "CoreMinimal.h"

/** Revision evidence and explicitly guarded saved-file staging for automation clients. All calls must execute on the game thread. */
class GRAPHXPORTMERGE_API FGraphXPortConflictReader
{
public:
    static FString ListConflicts();
    static FString WorkingState(const FString& Path);
    static bool StageResolvedFile(const FString& Path, const FString& ExpectedDiskHash);
    static void ClearRevisionCache();
    static FString Inspect(const FString& Path, const FString& Revision, const FString& Graph, int32 Offset, int32 Limit);
    static FString Compare(const FString& Path, const FString& Before, const FString& After, const FString& Graph, int32 Offset, int32 Limit);
};
