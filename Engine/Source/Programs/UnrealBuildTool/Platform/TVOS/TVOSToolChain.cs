// Copyright Epic Games, Inc. All Rights Reserved.


using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using System.Diagnostics;
using System.Security.AccessControl;
using System.Xml;
using System.Text;
using Ionic.Zip;
using Ionic.Zlib;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	class TVOSToolChainSettings : IOSToolChainSettings
	{
		public TVOSToolChainSettings() : base("AppleTVOS", "AppleTVSimulator")
		{
		}
	}

	class TVOSToolChain : IOSToolChain
	{
		public TVOSToolChain(ReadOnlyTargetRules InTarget, TVOSProjectSettings InProjectSettings)
			: base(InTarget, InProjectSettings, () => new TVOSToolChainSettings())
		{
		}

		public override string GetXcodeMinVersionParam()
		{
			return "tvos-version-min";
		}

		public override string GetArchitectureArgument(CppConfiguration Configuration, string UBTArchitecture)
		{
			// TV is only arm64
			return " -arch " + (UBTArchitecture == "-simulator" ? "i386" : "arm64");
		}
	}
}
