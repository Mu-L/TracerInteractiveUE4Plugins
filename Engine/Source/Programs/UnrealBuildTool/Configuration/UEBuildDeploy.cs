﻿// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Diagnostics;
using System.IO;

namespace UnrealBuildTool
{
	/// <summary>
	/// Base class to handle deploy of a target for a given platform
	/// </summary>
	abstract class UEBuildDeploy
	{
		/// <summary>
		/// Prepare the target for deployment
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		/// <returns>True if successful, false if not</returns>
		public virtual bool PrepTargetForDeployment(TargetReceipt Receipt)
		{
			return true;
		}
	}
}
