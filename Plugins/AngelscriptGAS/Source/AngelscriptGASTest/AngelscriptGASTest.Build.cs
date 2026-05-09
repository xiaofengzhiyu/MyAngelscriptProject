using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class AngelscriptGASTest : ModuleRules
	{
		public AngelscriptGASTest(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PrivateIncludePaths.Add(Path.Combine(PluginDirectory, "..", "Angelscript", "Source", "AngelscriptTest"));

			PublicDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AngelscriptRuntime",
				"AngelscriptGAS",
				"AngelscriptTest",
				"GameplayAbilities",
				"GameplayTags",
				"GameplayTasks",
			});

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(new string[]
				{
					"BlueprintGraph",
					"CQTest",
					"UnrealEd",
				});
			}
		}
	}
}
