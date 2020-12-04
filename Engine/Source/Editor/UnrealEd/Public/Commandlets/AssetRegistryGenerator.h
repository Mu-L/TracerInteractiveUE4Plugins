// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "AssetData.h"
#include "Misc/AssetRegistryInterface.h"
#include "AssetRegistryState.h"
#include "UObject/UObjectHash.h"

class FSandboxPlatformFile;
class IAssetRegistry;
class ITargetPlatform;
class IChunkDataGenerator;
class UChunkDependencyInfo;
struct FChunkDependencyTreeNode;

/**
 * Helper class for generating streaming install manifests
 */
class FAssetRegistryGenerator
{
public:
	/**
	 * Constructor
	 */
	FAssetRegistryGenerator(const ITargetPlatform* InPlatform);

	/**
	 * Destructor
	 */
	~FAssetRegistryGenerator();

	/**
	 * Initializes manifest generator - creates manifest lists, hooks up delegates.
	 */
	void Initialize(const TArray<FName> &StartupPackages);

	const ITargetPlatform* GetTargetPlatform() const { return TargetPlatform; }

	/** 
	 * Loads asset registry from a previous run that is used for iterative or DLC cooking
	 */
	bool LoadPreviousAssetRegistry(const FString& Filename);

	/**
	 * Computes differences between the previous asset registry and the current one 
	 *
	 * @param ModifiedPackages list of packages which existed before and now, but need to be recooked
	 * @param NewPackages list of packages that did not exist before, but exist now
	 * @param RemovedPackages list of packages that existed before, but do not any more
	 * @param IdenticalCookedPackages list of cooked packages that have not changed
	 * @param IdenticalUncookedPackages list of uncooked packages that have not changed. These were filtered out by platform or editor only
	 * @param bRecurseModifications if true, modified packages are recursed to X in X->Y->Z chains. Otherwise, only Y and Z are seen as modified
	* @param bRecurseModifications if true, modified script/c++ packages are recursed, if false only asset references are recursed
	 */
	void ComputePackageDifferences(TSet<FName>& ModifiedPackages, TSet<FName>& NewPackages, TSet<FName>& RemovedPackages, TSet<FName>& IdenticalCookedPackages, TSet<FName>& IdenticalUncookedPackages, bool bRecurseModifications, bool bRecurseScriptModifications);

	/**
	 * Tracks packages that were kept from a previous cook.
	 * Updates the current asset registry from the previous one for all kept packages.
	 */
	void UpdateKeptPackages(const TArray<FName>& InKeptPackages);

	/**
	 * GenerateChunkManifest 
	 * generate chunk manifest for the packages passed in using the asset registry to determine dependencies
	 *
	 * @param CookedPackages list of packages which were cooked
	 * @param DevelopmentOnlyPackages list of packages that were specifically not cooked, but to add to the development asset registry
	 * @param InSandboxFile sandbox to load/save data
	 * @param bGenerateStreamingInstallManifest should we build a streaming install manifest 
	 */
	void BuildChunkManifest(const TSet<FName>& CookedPackages, const TSet<FName>& DevelopmentOnlyPackages, FSandboxPlatformFile* InSandboxFile, bool bGenerateStreamingInstallManifest);

	/**
	 * Register a chunk data generator with this generator.
	 * @note Should be called prior to SaveManifests.
	 */
	void RegisterChunkDataGenerator(TSharedRef<IChunkDataGenerator> InChunkDataGenerator);

	/**
	* PreSave
	* Notify generator that we are about to save the registry and chunk manifests
	*/
	void PreSave(const TSet<FName>& InCookedPackages);

	/**
	* PostSave
	* Notify generator that we are finished saving registry
	*/
	void PostSave();

	/**
	 * ContainsMap
	 * Does this package contain a map file (determined by finding if this package contains a UWorld / ULevel object)
	 *
	 * @param PackageName long package name of the package we want to determine if contains a map 
	 * @return return if the package contains a UWorld / ULevel object (contains a map)
	 */
	bool ContainsMap(const FName& PackageName) const;

	/** 
	 * Returns editable version of the asset package state being generated 
	 */
	FAssetPackageData* GetAssetPackageData(const FName& PackageName);

	/**
	 * Adds a package to chunk manifest (just calls the other AddPackageToChunkManifestFunction with more parameters)
	 *
	 * @param Package Package to add to one of the manifests
	 * @param SandboxFilename Cooked sandbox path of the package to add to a manifest
	 * @param LastLoadedMapName Name of the last loaded map (can be empty)
	 * @param the SandboxPlatformFile used during cook
	 */
	void AddPackageToChunkManifest(const FName& PackageFName, const FString& PackagePathName, const FString& SandboxFilename, const FString& LastLoadedMapName, FSandboxPlatformFile* InSandboxFile);
	
	/**
	 * Add a package to the manifest but don't assign it to any chunk yet, packages which are not assigned by the end of the cook will be put into chunk 0
	 * 
	 * @param Package which is unassigned
	 * @param The sandbox file path of the package
	 */
	void AddUnassignedPackageToManifest(UPackage* Package, const FString& PackageSandboxPath );

	/**
	 * Deletes temporary manifest directories.
	 */
	void CleanManifestDirectories();

	/**
	 * Saves all generated manifests for each target platform.
	 * 
	 * @param InSandboxFile the InSandboxFile used during cook
	 * @param InExtraFlavorChunkSize the ChunkSize used during cooking for InExtraFlavor, value greater than 0 will trigger a cook for extra flavor with specified chunk size
	 */
	bool SaveManifests(FSandboxPlatformFile* InSandboxFile, int64 InExtraFlavorChunkSize = 0);

	/**
	* Saves generated asset registry data for each platform.
	*/
	bool SaveAssetRegistry(const FString& SandboxPath, bool bSerializeDevelopmentAssetRegistry = true, bool bForceNoFilterAssets = false);

	/** 
	 * Writes out CookerOpenOrder.log file 
	 */
	bool WriteCookerOpenOrder();

	/**
	 * Follows an assets dependency chain to build up a list of package names in the same order as the runtime would attempt to load them
	 * 
	 * @param InPackageName - The name of the package containing the asset to (potentially) add to the file order
	 * @param OutFileOrder - Output array which collects the package names, maintaining order
	 * @param OutEncounteredArray - Temporary collection of package names we've seen. Similar to OutFileOrder but updated BEFORE following dependencies so as to avoid circular references
	 * @param InPackageNameSet - The source package name list. Used to distinguish between dependencies on other packages and internal objects
	 * @param InTopLevelAssets - Names of packages containing top level assets such as maps
	 */
	void AddAssetToFileOrderRecursive(const FName& InPackageName, TArray<FName>& OutFileOrder, TSet<FName>& OutEncounteredNames, const TSet<FName>& InPackageNameSet, const TSet<FName>& InTopLevelAssets);

	/**
	 * Get pakchunk file index from ChunkID
	 *
	 * @param ChunkID
	 * @return Index of target pakchunk file
	 */
	int32 GetPakchunkIndex(int32 ChunkId) const;

	/**
	 * Returns the chunks
	 */
	void GetChunkAssignments(TArray<TSet<FName>>& OutAssignments) const;

private:

	/** State of the asset registry that is being built for this platform */
	FAssetRegistryState State;

	/** Base state, which is either a release build or an iterative cook */
	FAssetRegistryState PreviousState;

	/** List of packages that were loaded at startup */
	TSet<FName> StartupPackages;
	/** List of packages that were successfully cooked */
	TSet<FName> CookedPackages;
	/** List of packages that were filtered out from cooking */
	TSet<FName> DevelopmentOnlyPackages;
	/** List of packages that were kept from a previous cook */
	TArray<FName> KeptPackages;
	/** Map of Package name to Sandbox Paths */
	typedef TMap<FName, FString> FChunkPackageSet;
	/** Holds a reference to asset registry */
	IAssetRegistry& AssetRegistry;
	/** Platform to generate the manifest for */
	const ITargetPlatform* TargetPlatform;
	/** List of all asset packages that were created while loading the last package in the cooker. */
	TSet<FName> AssetsLoadedWithLastPackage;
	/** Lookup for the original ChunkID Mappings */
	TMap<FName, TArray<int32> > PackageChunkIDMap;
	/** Set of packages containing a map */
	TSet<FName> PackagesContainingMaps;
	/** Should the chunks be generated or only asset registry */
	bool bGenerateChunks;
	/** True if we should use the AssetManager, false to use the deprecated path */
	bool bUseAssetManager;
	/** Highest chunk id, being used for geneating dependency tree */
	int32 HighestChunkId;
	/** Array of Maps with chunks<->packages assignments */
	TArray<FChunkPackageSet*>		ChunkManifests;
	/** Map of packages that has not been assigned to chunks */
	FChunkPackageSet				UnassignedPackageSet;
	/** Map of all cooked Packages */
	FChunkPackageSet				AllCookedPackageSet;
	/** Array of Maps with chunks<->packages assignments. This version contains all dependent packages */
	TArray<FChunkPackageSet*>		FinalChunkManifests;
	/** Additional data generators used when creating chunks */
	TArray<TSharedRef<IChunkDataGenerator>> ChunkDataGenerators;
	/** Lookup table of used package names used when searching references. */
	TSet<FName>						InspectedNames;
	/** */
	UChunkDependencyInfo*			DependencyInfo;

	/** Required flags a dependency must have if it is to be followed when adding package dependencies to chunks.*/
	UE::AssetRegistry::EDependencyQuery DependencyQuery;

	/** Mapping from chunk id to pakchunk file index. If not defined, Pakchunk index will be the same as chunk id by default */
	TMap<int32, int32> ChunkIdPakchunkIndexMapping;

	struct FReferencePair
	{
		FReferencePair() {}

		FReferencePair(const FName& InName, uint32 InParentIndex)
			: PackageName(InName)
			, ParentNodeIndex(InParentIndex)
		{}

		bool operator == (const FReferencePair& RHS) const
		{
			return PackageName == RHS.PackageName;
		}

		FName		PackageName;
		uint32		ParentNodeIndex;
	};

	/**
	 * Updates disk data with CookedHash and DiskSize from previous asset registry
	 * for all packages kept from a previous cook.
	 */
	void UpdateKeptPackagesDiskData(const TArray<FName>& InKeptPackages);

	/**
	 * Updates AssetData with TagsAndValues from previous asset registry
	 * for all packages kept from a previous cook.
	 */
	void UpdateKeptPackagesAssetData();

	/**
	 * Updates AssetData with TagsAndValues corresponding to any collections 
	 * flagged for inclusion as asset registry tags.
	 */
	void UpdateCollectionAssetData();

	/**
	 * Adds a package to chunk manifest
	 * 
	 * @param The sandbox filepath of the package
	 * @param The package name
	 * @param The ID of the chunk to assign it to
	 */
	void AddPackageToManifest(const FString& PackageSandboxPath, FName PackageName, int32 ChunkId);

	/**
	* Remove a package from a chunk manifest. Does nothing if package doesn't exist in chunk.
	*
	* @param The package name
	* @param The ID of the chunk to assign it to
	*/
	void RemovePackageFromManifest(FName PackageName, int32 ChunkId);

	/**
	 * Walks the dependency graph of assets and assigns packages to correct chunks.
	 * 
	 * @param the InSandboxFile used during cook
	 */
	void FixupPackageDependenciesForChunks(FSandboxPlatformFile* InSandboxFile);

	/**
	 * Attaches encryption key guids into the registry data for encrypted primary assets
	 */
	void InjectEncryptionData(FAssetRegistryState& TargetState);

	void AddPackageAndDependenciesToChunk(FChunkPackageSet* ThisPackageSet, FName InPkgName, const FString& InSandboxFile, int32 PakchunkIndex, FSandboxPlatformFile* SandboxPlatformFile);

	/**
	 * Returns the path of the temporary packaging directory for the specified platform.
	 */
	FString GetTempPackagingDirectoryForPlatform(const FString& Platform) const
	{
		return FPaths::ProjectSavedDir() / TEXT("TmpPackaging") / Platform;
	}

	/**
	 * 
	 */
	int64 GetMaxChunkSizePerPlatform( const ITargetPlatform* Platform ) const;

	/**
	* Returns an array of chunks ID for a package name that have been assigned during the cook process.
	*/
	FORCEINLINE TArray<int32> GetExistingPackageChunkAssignments(FName PackageFName)
	{
		TArray<int32> ExistingChunkIDs;
		for (uint32 ChunkIndex = 0, MaxChunk = ChunkManifests.Num(); ChunkIndex < MaxChunk; ++ChunkIndex)
		{
			if (ChunkManifests[ChunkIndex] && ChunkManifests[ChunkIndex]->Contains(PackageFName))
			{
				ExistingChunkIDs.AddUnique(ChunkIndex);
			}
		}

		if ( StartupPackages.Contains(PackageFName ))
		{
			ExistingChunkIDs.AddUnique(0);
		}

		return ExistingChunkIDs;
	}

	/**
	* Returns an array of chunks IDs for a package that have been assigned in the editor.
	*/
	FORCEINLINE TArray<int32> GetAssetRegistryChunkAssignments(const FName& PackageFName)
	{
		TArray<int32> RegistryChunkIDs;
		auto* FoundIDs = PackageChunkIDMap.Find(PackageFName);
		if (FoundIDs)
		{
			RegistryChunkIDs = *FoundIDs;
		}
		return RegistryChunkIDs;
	}

	/** Generate manifest for a single package */
	void GenerateChunkManifestForPackage(const FName& PackageFName, const FString& PackagePathName, const FString& SandboxFilename, const FString& LastLoadedMapName, FSandboxPlatformFile* InSandboxFile);

	/** Deletes the temporary packaging directory for the specified platform */
	bool CleanTempPackagingDirectory(const FString& Platform) const;

	/** Returns true if the specific platform desires a chunk manifest */
	bool ShouldPlatformGenerateStreamingInstallManifest(const ITargetPlatform* Platform) const;

	/** Generates and saves streaming install chunk manifest */
	bool GenerateStreamingInstallManifest(int64 InExtraFlavorChunkSize, FSandboxPlatformFile* InSandboxFile);

	/** Gather a list of dependencies required by to completely load this package */
	bool GatherAllPackageDependencies(FName PackageName, TArray<FName>& DependentPackageNames);

	/** Gather the list of dependencies that link the source to the target.  Output array includes the target */
	bool GetPackageDependencyChain(FName SourcePackage, FName TargetPackage, TSet<FName>& VisitedPackages, TArray<FName>& OutDependencyChain);

	/** Get an array of Packages this package will import */
	bool GetPackageDependencies(FName PackageName, TArray<FName>& DependentPackageNames, UE::AssetRegistry::EDependencyQuery InDependencyQuery);

	/** Save a CSV dump of chunk asset information, if bWriteIndividualFiles is true it writes a CSV per chunk in addition to AllChunksInfo */
	bool GenerateAssetChunkInformationCSV(const FString& OutputPath, bool bWriteIndividualFiles = false);

	/** Finds the asset belonging to ChunkID with the smallest number of links to Packages In PackageNames */
	void FindShortestReferenceChain(TArray<FReferencePair> PackageNames, int32 ChunkID, uint32& OutParentIndex, FString& OutChainPath);

	/** Helper function for FindShortestReferenceChain */
	FString	GetShortestReferenceChain(FName PackageName, int32 ChunkID);

	/** Deprecated method to remove redundant chunks */
	void ResolveChunkDependencyGraph(const FChunkDependencyTreeNode& Node, const TSet<FName>& BaseAssetSet, TArray<TArray<FName>>& OutPackagesMovedBetweenChunks);

	/** Helper function to verify Chunk asset assignment is valid */
	bool CheckChunkAssetsAreNotInChild(const FChunkDependencyTreeNode& Node);

	/** Helper function to create a given collection. */
	bool CreateOrEmptyCollection(FName CollectionName);

	/** Helper function to fill a given collection with a set of packages */
	void WriteCollection(FName CollectionName, const TArray<FName>& PackageNames);
	
	/** Initialize ChunkIdPakchunkIndexMapping and PakchunkIndexChunkIdMapping. */
	void InitializeChunkIdPakchunkIndexMapping();
};