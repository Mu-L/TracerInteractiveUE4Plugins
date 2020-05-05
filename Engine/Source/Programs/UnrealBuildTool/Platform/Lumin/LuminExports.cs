﻿// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Interface to allow exposing public methods from the Android deployment context to other assemblies
	/// </summary>
	public interface ILuminDeploy
	{
		/// <summary>
		/// 
		/// </summary>
		/// <param name="Architectures"></param>
		/// <param name="inPluginExtraData"></param>
		void SetLuminPluginData(List<string> Architectures, List<string> inPluginExtraData);
		// @todo Lumin: Lumin plugins?

		/// <summary>
		/// 
		/// </summary>
		/// <param name="Receipt"></param>
		void InitUPL(TargetReceipt Receipt);

		/// <summary>
		/// 
		/// </summary>
		string StageFiles();


		/// <summary>
		/// 
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <param name="ProjectName"></param>
		/// <param name="ProjectDirectory"></param>
		/// <param name="ExecutablePath"></param>
		/// <param name="EngineDirectory"></param>
		/// <param name="bForDistribution"></param>
		/// <param name="CookFlavor"></param>
		/// <param name="bIsDataDeploy"></param>
		/// <param name="MpkName"></param>
		/// <returns></returns>
		bool PrepForUATPackageOrDeploy(FileReference ProjectFile, string ProjectName, DirectoryReference ProjectDirectory, string ExecutablePath, string EngineDirectory, bool bForDistribution, string CookFlavor, bool bIsDataDeploy, string MpkName);

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ProjectName"></param>
		/// <returns></returns>
		string GetPackageName(string ProjectName);

		/// <summary>
		/// Directory path in the MPK where the icon model assets will be staged.
		/// </summary>
		/// <returns>Directory path relative to root of MPK</returns>
		string GetIconModelStagingPath();

		/// <summary>
		/// Directory path in the MPK where the icon portal assets will be staged.
		/// </summary>
		/// <returns>Directory path relative to root of MPK</returns>
		string GetIconPortalStagingPath();

		/// <summary>
		/// Path of the certificate to sign the MPK with.
		/// </summary>
		/// <returns>Certificate file path relative to root of the project</returns>
		string GetProjectRelativeCertificatePath();

		/// <summary>
		/// Whether the app should use vulkan
		/// </summary>
		/// <returns>true if app should use vulkan, false otherwise</returns>
		bool UseVulkan();

		/// <summary>
		/// Path of the folder containing the vulkan validation layers.
		/// </summary>
		/// <returns>Path of the folder containing the vulkan validation layers.</returns>
		string GetVulkanValdationLayerLibsDir();
	}

	/// <summary>
	/// Public Android functions exposed to UAT
	/// </summary>
	public static class LuminExports
	{
		/// <summary>
		/// 
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <returns></returns>
		public static IAndroidToolChain CreateToolChain(FileReference ProjectFile)
		{
			return new LuminToolChain(ProjectFile);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <returns></returns>
		public static ILuminDeploy CreateDeploymentHandler(FileReference ProjectFile)
		{
			return new UEDeployLumin(ProjectFile);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="SourceFile"></param>
		/// <param name="TargetFile"></param>
		public static void StripSymbols(FileReference SourceFile, FileReference TargetFile)
		{
			LuminToolChain ToolChain = new LuminToolChain(null);
			ToolChain.StripSymbols(SourceFile, TargetFile);
		}
	}
}
