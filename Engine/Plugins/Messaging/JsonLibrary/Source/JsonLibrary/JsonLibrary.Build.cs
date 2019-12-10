// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class JsonLibrary : ModuleRules
	{
		public JsonLibrary(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"Json"
				}
			);
		}
	}
}
