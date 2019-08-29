// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// A module that is never compiled by us, and is only used to group include paths and libraries into a dependency unit.
	/// </summary>
	class UEBuildModuleExternal : UEBuildModule
	{
		public UEBuildModuleExternal(ModuleRules Rules)
			: base(Rules)
		{
		}

		// UEBuildModule interface.
		public override List<FileItem> Compile(ReadOnlyTargetRules Target, UEToolChain ToolChain, CppCompileEnvironment CompileEnvironment, ISourceFileWorkingSet WorkingSet, TargetMakefile Makefile)
		{
			return new List<FileItem>();
		}
	}
}
