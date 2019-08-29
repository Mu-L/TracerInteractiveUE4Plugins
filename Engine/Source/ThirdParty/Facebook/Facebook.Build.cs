// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Facebook : ModuleRules
{
	public Facebook(ReadOnlyTargetRules Target) : base(Target)
    {
		Type = ModuleType.External;

		// Additional Frameworks and Libraries for Android found in OnlineSubsystemFacebook_UPL.xml
        if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicDefinitions.Add("WITH_FACEBOOK=1");
			PublicDefinitions.Add("UE4_FACEBOOK_VER=4.18");

            // These are iOS system libraries that Facebook depends on (FBAudienceNetwork, FBNotifications)
            PublicFrameworks.AddRange(
            new string[] {
                "ImageIO"
            });

            // More dependencies for Facebook (FBAudienceNetwork, FBNotifications)
            PublicAdditionalLibraries.AddRange(
            new string[] {
                "xml2"
            });

			PublicAdditionalFrameworks.Add(
				new UEBuildFramework(
					"AccountKit",
					"IOS/FacebookSDK/AccountKit.embeddedframework.zip",
					"AccountKit.framework/AccountKitStrings.bundle"
				)
			);

			PublicAdditionalFrameworks.Add(
				new UEBuildFramework(
					"Bolts",
					"IOS/FacebookSDK/Bolts.embeddedframework.zip"
				)
			);

			// Access to Facebook notifications
			PublicAdditionalFrameworks.Add(
				new UEBuildFramework(
					"FBNotifications",
					"IOS/FacebookSDK/FBNotifications.embeddedframework.zip"
				)
			);

			// Access to Facebook core
			PublicAdditionalFrameworks.Add(
				new UEBuildFramework(
					"FBSDKCoreKit",
					"IOS/FacebookSDK/FBSDKCoreKit.embeddedframework.zip",
					"FBSDKCoreKit.framework/Resources/FacebookSDKStrings.bundle"
				)
			);

			// Access to Facebook login
			PublicAdditionalFrameworks.Add(
				new UEBuildFramework(
					"FBSDKLoginKit",
					"IOS/FacebookSDK/FBSDKLoginKit.embeddedframework.zip"
				)
			);


            // commenting out over if(false) for #jira FORT-77943 per Peter.Sauerbrei prior change with CL 3960071
            //// Access to Facebook places
            //PublicAdditionalFrameworks.Add(
            //	new UEBuildFramework(
            //		"FBSDKPlacesKit",
            //		"IOS/FacebookSDK/FBSDKPlacesKit.embeddedframework.zip"
            //	)
            //);

            // Access to Facebook messenger sharing
            PublicAdditionalFrameworks.Add(
				new UEBuildFramework(
					"FBSDKMessengerShareKit",
					"IOS/FacebookSDK/FBSDKMessengerShareKit.embeddedframework.zip"
				)
			);

			// Access to Facebook sharing
			PublicAdditionalFrameworks.Add(
				new UEBuildFramework(
					"FBSDKShareKit",
					"IOS/FacebookSDK/FBSDKShareKit.embeddedframework.zip"
				)
			);
		}
	}
}

