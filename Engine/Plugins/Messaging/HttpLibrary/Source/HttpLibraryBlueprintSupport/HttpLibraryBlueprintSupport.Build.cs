// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class HttpLibraryBlueprintSupport : ModuleRules
	{
		public HttpLibraryBlueprintSupport(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{ 
					"Core", 
					"CoreUObject", 
					"Engine",
					"BlueprintGraph",
					"HttpLibrary"
				}
			);
		}
	}
}
