//
// Copyright (C) Valve Corporation. All rights reserved.
//

namespace UnrealBuildTool.Rules
{
	public class SteamAudioEditor : ModuleRules
	{
		public SteamAudioEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.AddRange(
				new string[] {
					"SteamAudioEditor/Private",
					"SteamAudio/Private",
				}
			);

			PublicIncludePaths.AddRange(
				new string[] {
				}
			);


			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"UnrealEd",
					"LevelEditor",
					"EditorStyle",
					"RenderCore",
					"RHI",
					"AudioEditor",
					"AudioMixer",
					"SteamAudio"
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
				    "AssetTools",
				    "Landscape"
			});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Slate",
					"SlateCore",
					"UnrealEd",
					"AudioEditor",
					"LevelEditor",
					"Landscape",
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"PropertyEditor",
					"Projects",
					"EditorStyle",
					"SteamAudio"
				 }
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "libPhonon");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11Audio");
		}
	}
}