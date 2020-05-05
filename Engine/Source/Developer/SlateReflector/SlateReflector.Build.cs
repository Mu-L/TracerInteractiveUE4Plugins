// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateReflector : ModuleRules
{
	public SlateReflector(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"ApplicationCore",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"Json",
				"AssetRegistry",
                "MessageLog"
            }
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
                "Messaging",
				"MessagingCommon",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"Messaging",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Developer/SlateReflector/Private",
				"Developer/SlateReflector/Private/Models",
				"Developer/SlateReflector/Private/Widgets",
			}
		);

		// Editor builds include SessionServices to populate the remote target drop-down for remote widget snapshots
		if (Target.Type == TargetType.Editor)
		{
			PublicDefinitions.Add("SLATE_REFLECTOR_HAS_SESSION_SERVICES=1");

			PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "PropertyEditor",
                }
			);

            PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"SessionServices",
                }
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"SessionServices",
				}
			);
        }
		else
		{
			PublicDefinitions.Add("SLATE_REFLECTOR_HAS_SESSION_SERVICES=0");
		}

		// DesktopPlatform is only available for Editor and Program targets (running on a desktop platform)
		bool IsDesktopPlatformType = Target.Platform == UnrealBuildTool.UnrealTargetPlatform.Win32
			|| Target.Platform == UnrealBuildTool.UnrealTargetPlatform.Win64
			|| Target.Platform == UnrealBuildTool.UnrealTargetPlatform.Mac
			|| Target.Platform == UnrealBuildTool.UnrealTargetPlatform.Linux;
		if (Target.Type == TargetType.Editor || (Target.Type == TargetType.Program && IsDesktopPlatformType))
		{
			PublicDefinitions.Add("SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM=1");

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"DesktopPlatform",
				}
			);
		}
		else
		{
			PublicDefinitions.Add("SLATE_REFLECTOR_HAS_DESKTOP_PLATFORM=0");
		}

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrecompileForTargets = PrecompileTargetsType.Any;
		}
	}
}
