using System;
using UnrealBuildTool;

public class RedwebBP : ModuleRules
{
    public RedwebBP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
            "HTTP"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.Add("winhttp.lib");
        }

        // Preserve per-line attribution for the native coverage build only.
        if (Environment.GetEnvironmentVariable("REDWEBBP_NATIVE_COVERAGE") == "1")
        {
            OptimizeCode = CodeOptimization.Never;
            PrivateDefinitions.Add("REDWEBBP_NATIVE_COVERAGE=1");
        }
    }
}
