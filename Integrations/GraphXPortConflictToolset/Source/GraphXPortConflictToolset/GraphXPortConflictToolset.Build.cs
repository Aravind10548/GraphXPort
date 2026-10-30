using UnrealBuildTool;
public class GraphXPortConflictToolset : ModuleRules
{
    public GraphXPortConflictToolset(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry", "GraphXPortMerge", "UnrealMCP", "Json" });
    }
}
