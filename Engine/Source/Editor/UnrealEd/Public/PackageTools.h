// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PackageTools.h: Object-related utilities

=============================================================================*/

#pragma once

#include "CoreMinimal.h"

class ULevel;

namespace PackageTools
{
	/**
	 * Filters the global set of packages.
	 *
	 * @param	OutGroupPackages			The map that receives the filtered list of group packages.
	 * @param	OutPackageList				The array that will contain the list of filtered packages.
	 */
	UNREALED_API void GetFilteredPackageList(TSet<UPackage*>& OutFilteredPackageMap);
	
	/**
	 * Fills the OutObjects list with all valid objects that are supported by the current
	 * browser settings and that reside withing the set of specified packages.
	 *
	 * @param	InPackages			Filters objects based on package.
	 * @param	OutObjects			[out] Receives the list of objects
	 */
	UNREALED_API void GetObjectsInPackages( const TArray<UPackage*>* InPackages, TArray<UObject*>& OutObjects );

	/**
	 * Handles fully loading passed in packages.
	 *
	 * @param	TopLevelPackages	Packages to be fully loaded.
	 * @param	OperationText		Text describing the operation; appears in the warning string presented to the user.
	 * 
	 * @return true if all packages where fully loaded, false otherwise
	 */
	UNREALED_API bool HandleFullyLoadingPackages( const TArray<UPackage*>& TopLevelPackages, const FText& OperationText );


	/**
	 * Loads the specified package file (or returns an existing package if it's already loaded.)
	 *
	 * @param	InFilename	File name of package to load
	 *
	 * @return	The loaded package (or NULL if something went wrong.)
	 */
	UNREALED_API UPackage* LoadPackage( FString InFilename );

	/**
	 * Helper function that attempts to unload the specified top-level packages.
	 *
	 * @param	PackagesToUnload	the list of packages that should be unloaded
	 *
	 * @return	true if the set of loaded packages was changed
	 */
	UNREALED_API bool UnloadPackages( const TArray<UPackage*>& PackagesToUnload );

	/**
	 * Helper function that attempts to unload the specified top-level packages.
	 *
	 * @param	PackagesToUnload	the list of packages that should be unloaded
	 * @param	OutErrorMessage		An error message specifying any problems with unloading packages
	 *
	 * @return	true if the set of loaded packages was changed
	 */
	UNREALED_API bool UnloadPackages( const TArray<UPackage*>& PackagesToUnload, FText& OutErrorMessage );

	/**
	 * Helper function that attempts to reload the specified top-level packages.
	 *
	 * @param	PackagesToReload	The list of packages that should be reloaded
	 *
	 * @return	true if the set of loaded packages was changed
	 */
	UNREALED_API bool ReloadPackages( const TArray<UPackage*>& PackagesToReload );

	/**
	 * Helper function that attempts to reload the specified top-level packages.
	 *
	 * @param	PackagesToReload	The list of packages that should be reloaded
	 * @param	OutErrorMessage		An error message specifying any problems with reloading packages
	 * @param	bInteractive		Whether the function is allowed to ask the user questions (such as whether to reload dirty packages)
	 *
	 * @return	true if the set of loaded packages was changed
	 */
	UNREALED_API bool ReloadPackages( const TArray<UPackage*>& PackagesToReload, FText& OutErrorMessage, const bool bInteractive = true );

	/**
	 *	Exports the given packages to files.
	 *
	 * @param	PackagesToExport		The set of packages to export.
	 * @param	ExportPath				receives the value of the path the user chose for exporting.
	 * @param	bUseProvidedExportPath	If true and ExportPath is specified, use ExportPath as the user's export path w/o prompting for a directory, where applicable
	 */
	UNREALED_API void ExportPackages( const TArray<UPackage*>& PackagesToExport, FString* ExportPath=NULL, bool bUseProvidedExportPath = false );

	/**
	 * Wrapper method for multiple objects at once.
	 *
	 * @param	TopLevelPackages		the packages to be export
	 * @param	LastExportPath			the path that the user last exported assets to
	 * @param	FilteredClasses			if specified, set of classes that should be the only types exported if not exporting to single file
	 * @param	bUseProvidedExportPath	If true, use LastExportPath as the user's export path w/o prompting for a directory, where applicable
	 *
	 * @return	the path that the user chose for the export.
	 */
	UNREALED_API FString DoBulkExport(const TArray<UPackage*>& TopLevelPackages, FString LastExportPath, const TSet<UClass*>* FilteredClasses = NULL, bool bUseProvidedExportPath = false );

	/** Helper function that attempts to check out the specified top-level packages. */
	UNREALED_API void CheckOutRootPackages( const TArray<UPackage*>& Packages );


	/**
	 * Checks if the passed in path is in an external directory. I.E Ones not found automatically in the content directory
	 *
	 * @param	PackagePath	Path of the package to check, relative or absolute
	 * @return	true if PackagePath points to an external location
	 */
	UNREALED_API bool IsPackagePathExternal(const FString& PackagePath);

	/**
	 * Checks if the passed in package's filename is in an external directory. I.E Ones not found automatically in the content directory
	 *
	 * @param	Package	The package to check
	 * @return	true if the package points to an external filename
	 */
	UNREALED_API bool IsPackageExternal(const UPackage& Package);

	/**
	 * Checks if the passed in packages have any references to  externally loaded packages.  I.E Ones not found automatically in the content directory
	 *
	 * @param	PackagesToCheck					The packages to check
	 * @param	OutPackagesWithExternalRefs		Optional list of packages that do have external references
	 * @param	LevelToCheck					The ULevel to check
	 * @param	OutObjectsWithExternalRefs		List of objects gathered from within the given ULevel that have external references
	 * @return	true if PackagesToCheck has references to an externally loaded package
	 */
	UNREALED_API bool CheckForReferencesToExternalPackages(const TArray<UPackage*>* PackagesToCheck, TArray<UPackage*>* OutPackagesWithExternalRefs, ULevel* LevelToCheck=NULL, TArray<UObject*>* OutObjectsWithExternalRefs=NULL );

	/** Saves all the dirty packages for the specified objects*/
	UNREALED_API bool SavePackagesForObjects(const TArray<UObject*>& ObjectsToSave);

	/**
	 * Checks if the package has only one asset which shares its name
	 *
	 * @param Package The package to check
	 * @return true if the package has only one asset which shares the name of the package
	 */
	UNREALED_API bool IsSingleAssetPackage (const FString& Package);

	/** Replaces all invalid package name characters with _ */
	UNREALED_API FString SanitizePackageName(const FString& InPackageName);
}
