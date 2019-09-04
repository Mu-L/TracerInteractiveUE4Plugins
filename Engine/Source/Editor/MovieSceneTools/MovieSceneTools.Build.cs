// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MovieSceneTools : ModuleRules
{
	public MovieSceneTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
            new string[] {
                "Editor/MovieSceneTools/Private",
                "Editor/MovieSceneTools/Private/CurveKeyEditors",
                "Editor/MovieSceneTools/Private/TrackEditors",
				"Editor/MovieSceneTools/Private/TrackEditors/PropertyTrackEditors",
                "Editor/MovieSceneTools/Private/TrackEditorThumbnail",
				"Editor/MovieSceneTools/Private/Sections",
                "Editor/UnrealEd/Private",	//compatibility for FBX importer
            }
        );

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
                "InputCore",
                "MovieSceneCapture",
				"UnrealEd",
				"Sequencer"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "ActorPickerMode",
                "CinematicCamera",
                "CurveEditor",
                "DesktopPlatform",
                "Json",
                "JsonUtilities",
				"LevelSequence",
                "MessageLog",
				"MovieScene",
				"MovieSceneTracks",
				"BlueprintGraph",
				"KismetCompiler",
                "GraphEditor",
                "ContentBrowser",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"EditorWidgets",
				"PropertyEditor",
                "MaterialEditor",
				"RenderCore",
				"RHI",
				"SequenceRecorder",
				"TimeManagement",
                "AnimationCore",
				"TimeManagement",
                "XmlParser",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
                "AssetRegistry",
				"AssetTools",
				"Sequencer",
                "Settings",
				"SceneOutliner",
                "MainFrame",
                "UnrealEd",
                "Analytics",
            }
        );

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
                "AssetRegistry",
				"AssetTools",
				"SceneOutliner",
                "MainFrame",
			}
		);

        CircularlyReferencedDependentModules.AddRange(
            new string[] {
                "Sequencer",
            }
        );

        AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
    }
}
