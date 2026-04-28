using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class AngelscriptLoader : ModuleRules
	{
		public AngelscriptLoader(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"Engine",
				"AngelscriptRuntime",
			});

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(new string[]
				{
					"AngelscriptEditor",
				});
			}
		}
	}
}
