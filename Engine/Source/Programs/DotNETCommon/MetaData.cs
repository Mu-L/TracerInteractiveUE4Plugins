// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using System.Reflection;
using System.Runtime.InteropServices;
using System.Resources;

// These are the assembly properties for all tools
[assembly: AssemblyCompany( "Epic Games, Inc." )]
[assembly: AssemblyProduct( "UE4" )]
[assembly: AssemblyCopyright( "Copyright 1998-2018 Epic Games, Inc. All Rights Reserved." )]
[assembly: AssemblyTrademark( "" )]

// Use a neutral culture to avoid some localisation issues
[assembly: AssemblyCulture( "" )]

[assembly: ComVisible( false )]
[assembly: NeutralResourcesLanguageAttribute( "" )]

#if !SPECIFIC_VERSION
// Automatically generate a version number based on the time of compilation
[assembly: AssemblyVersion( "4.0.0.0" )]
[assembly: AssemblyInformationalVersion("4.0.0")]
#endif

