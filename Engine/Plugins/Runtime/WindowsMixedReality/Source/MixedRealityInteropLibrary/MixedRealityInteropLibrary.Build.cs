// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class MixedRealityInteropLibrary : ModuleRules
{
	public MixedRealityInteropLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibName = "MixedRealityInterop";
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				LibName += "Debug";
			}
			string DLLName = LibName + ".dll";
			LibName += ".lib";

			string InteropLibPath = EngineDirectory + "/Source/ThirdParty/WindowsMixedRealityInterop/Lib/x64/";
			PublicAdditionalLibraries.Add(InteropLibPath + LibName);

			// Delay-load the DLL, so we can load it from the right place first
			PublicDelayLoadDLLs.Add(DLLName);
			RuntimeDependencies.Add(EngineDirectory + "/Binaries/ThirdParty/Windows/x64/" + DLLName);
			
			// Hologram remoting dlls
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string[] Dlls = { "Microsoft.Holographic.AppRemoting.dll", "PerceptionDevice.dll", "Microsoft.MixedReality.QR.dll" };

				foreach(var Dll in Dlls)
				{
					PublicDelayLoadDLLs.Add(Dll);
					RuntimeDependencies.Add(EngineDirectory + "/Binaries/ThirdParty/Windows/x64/" + Dll);
				}

                string[] HL1Dlls = { "HolographicStreamerDesktop.dll", "Microsoft.Perception.Simulation.dll", "PerceptionSimulationManager.dll" };

                foreach (var Dll in HL1Dlls)
                {
                    PublicDelayLoadDLLs.Add(Dll);
                    RuntimeDependencies.Add(EngineDirectory + "/Binaries/Win64/" + Dll);
                }
            }
		}
		else if(Target.Platform == UnrealTargetPlatform.HoloLens)
		{
            string InteropLibPath = Target.UEThirdPartySourceDirectory + "/WindowsMixedRealityInterop/Lib/" + Target.WindowsPlatform.GetArchitectureSubpath() + "/";
            PublicAdditionalLibraries.Add(InteropLibPath + (Target.Configuration == UnrealTargetConfiguration.Debug ? "MixedRealityInteropHoloLensDebug.lib" : "MixedRealityInteropHoloLens.lib"));
		}
	}
}
