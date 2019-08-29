// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class HttpLibrary : ModuleRules
	{
		public HttpLibrary(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"Http",
					"JsonLibrary"
				}
			);
		}
	}
}
