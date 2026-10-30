using UnrealBuildTool;

public class GraphXPortMerge : ModuleRules
{
    public GraphXPortMerge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ApplicationCore",
            "Json",
            "BlueprintGraph",
            "GraphEditor",
            "InputCore",
            "Kismet",
            "Projects",
            "PropertyEditor",
            "RHI",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UMG",
            "UMGEditor",
            "UnrealEd",
            "WorkspaceMenuStructure"
        });
    }
}
