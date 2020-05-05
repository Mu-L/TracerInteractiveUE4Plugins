// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNativeCodeGenUtils.h"
#include "BlueprintCompilationManager.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "BlueprintNativeCodeGenManifest.h"
#include "Kismet2/KismetReinstanceUtilities.h"
#include "KismetCompilerModule.h"
#include "ModuleDescriptor.h"
#include "PluginDescriptor.h"
#include "GameProjectUtils.h"
#include "Misc/ScopeExit.h"
#include "FindInBlueprintManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "PlatformInfo.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY(LogBlueprintCodeGen)

/*******************************************************************************
 * BlueprintNativeCodeGenUtilsImpl
 ******************************************************************************/

namespace BlueprintNativeCodeGenUtilsImpl
{
	static FString CoreModuleName   = TEXT("Core");
	static FString EngineModuleName = TEXT("Engine");
	static FString EngineHeaderFile = TEXT("Engine.h");

	// Used to cache the set of plugin dependencies.
	static TSet<FString> PluginDependencies;
	
	/**
	 * Creates and fills out a new .uplugin file for the converted assets.
	 * 
	 * @param  TargetPaths	Defines the file path/name for the plugin file.
	 * @return True if the file was successfully saved, otherwise false.
	 */
	static bool GeneratePluginDescFile(const FBlueprintNativeCodeGenPaths& TargetPaths);

	/**
	 * Creates a module implementation and header file for the converted assets'
	 * module (provides a IMPLEMENT_MODULE() declaration, which is required for 
	 * the module to function).
	 * 
	 * @param  TargetPaths    Defines the file path/name for the target files.
	 * @param  bExcludeMonolithicEngineHeaders	Whether or not to exclude monolithic engine headers.
	 * @return True if the files were successfully generated, otherwise false.
	 */
	static bool GenerateModuleSourceFiles(const FBlueprintNativeCodeGenPaths& TargetPaths, bool bExcludeMonolithicEngineHeaders);

	/**
	 * Creates and fills out a new .Build.cs file for the plugin's runtime module.
	 * 
	 * @param  Manifest    Defines where the module file should be saved, what it should be named, etc..
	 * @return True if the file was successfully saved, otherwise false.
	 */
	static bool GenerateModuleBuildFile(const FBlueprintNativeCodeGenManifest& Manifest);

	/**
	 * Determines what the expected native class will be for an asset that was  
	 * or will be converted.
	 * 
	 * @param  ConversionRecord    Identifies the asset's original type (which is used to infer the replacement type from).
	 * @return Either a class, enum, or struct class (depending on the asset's type).
	 */
	static UClass* ResolveReplacementType(const FConvertedAssetRecord& ConversionRecord);

	/** */
	static FString NativizedDependenciesFileName() { return TEXT("NativizedAssets_Dependencies"); }
	/** */
	static bool GenerateNativizedDependenciesSourceFiles(const FBlueprintNativeCodeGenPaths& TargetPaths, bool bExcludeMonolithicEngineHeaders);
}

//------------------------------------------------------------------------------
static bool BlueprintNativeCodeGenUtilsImpl::GeneratePluginDescFile(const FBlueprintNativeCodeGenPaths& TargetPaths)
{
	FPluginDescriptor PluginDesc;
	
	const FString FilePath = TargetPaths.PluginFilePath();
	FText ErrorMessage;
	// attempt to load an existing plugin (in case it has existing source for another platform that we wish to keep)
	PluginDesc.Load(FilePath, ErrorMessage);

	PluginDesc.FriendlyName = TargetPaths.GetPluginName();
	PluginDesc.CreatedBy    = TEXT("Epic Games, Inc.");
	PluginDesc.CreatedByURL = TEXT("http://epicgames.com");
	PluginDesc.Description  = TEXT("A programatically generated plugin which contains source files produced from Blueprint assets. The aim of this is to help performance by eliminating script overhead for the converted assets (using the source files in place of thier coresponding assets).");
	PluginDesc.DocsURL      = TEXT("@TODO");
	PluginDesc.SupportURL   = TEXT("https://answers.unrealengine.com/");
	PluginDesc.Category     = TEXT("Intermediate");
	PluginDesc.EnabledByDefault  = EPluginEnabledByDefault::Enabled;
	PluginDesc.bCanContainContent = false;
	PluginDesc.bIsHidden    = true; 

	const FName ModuleName = *TargetPaths.RuntimeModuleName();
	FModuleDescriptor* ModuleDesc = PluginDesc.Modules.FindByPredicate([ModuleName](const FModuleDescriptor& Module)->bool
		{
			return (Module.Name == ModuleName);
		}
	);
	if (ModuleDesc == nullptr)
	{
		ModuleDesc = &PluginDesc.Modules[ PluginDesc.Modules.Add(FModuleDescriptor()) ];
	}
	else
	{
		ModuleDesc->WhitelistPlatforms.Empty();
		ModuleDesc->WhitelistTargets.Empty();
	}
	if (ensure(ModuleDesc))
	{
		ModuleDesc->Name = ModuleName;
		ModuleDesc->Type = EHostType::CookedOnly;
		// load at startup (during engine init), after game modules have been loaded 
		ModuleDesc->LoadingPhase = ELoadingPhase::Default;

		const FName PlatformName = TargetPaths.GetTargetPlatformName();
		for (const PlatformInfo::FPlatformInfo& PlatformInfo : PlatformInfo::GetPlatformInfoArray())
		{
			if (PlatformInfo.TargetPlatformName == PlatformName)
			{
				// We use the 'UBTTargetId' because this white-list expects the 
				// string to correspond to UBT's UnrealTargetPlatform enum (and by proxy, FPlatformMisc::GetUBTPlatform)
				ModuleDesc->WhitelistPlatforms.AddUnique(PlatformInfo.UBTTargetId.ToString());

				// Hack to allow clients for PS4/XboxOne (etc.) to build the nativized assets plugin
				const bool bIsClientValidForPlatform = PlatformInfo.UBTTargetId == TEXT("Win32") ||
					PlatformInfo.UBTTargetId == TEXT("Win64") ||
					PlatformInfo.UBTTargetId == TEXT("Linux") ||
					PlatformInfo.UBTTargetId == TEXT("LinuxAArch64") ||
					PlatformInfo.UBTTargetId == TEXT("Mac");

				// should correspond to UnrealBuildTool::TargetType in TargetRules.cs
				switch (PlatformInfo.PlatformType)
				{
				case EBuildTargetType::Game:
					ModuleDesc->WhitelistTargets.AddUnique(EBuildTargetType::Game);

					// Hack to allow clients for PS4/XboxOne (etc.) to build the nativized assets plugin
					if(!bIsClientValidForPlatform)
					{
						// Also add "Client" target
						ModuleDesc->WhitelistTargets.AddUnique(EBuildTargetType::Client);
					}
					break;

				case EBuildTargetType::Client:
					ModuleDesc->WhitelistTargets.AddUnique(EBuildTargetType::Client);
					break;

				case EBuildTargetType::Server:
					ModuleDesc->WhitelistTargets.AddUnique(EBuildTargetType::Server);
					break;

				case EBuildTargetType::Editor:
					ensureMsgf(PlatformInfo.PlatformType != EBuildTargetType::Editor, TEXT("Nativized Blueprint plugin is for cooked projects only - it isn't supported in editor builds."));
					break;
				};				
			}
		}
	}

	// Add plugin dependencies to the descriptor
	for (const FString& PluginName : PluginDependencies)
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (Plugin.IsValid())
		{
			FPluginReferenceDescriptor PluginRefDesc(Plugin->GetName(), Plugin->IsEnabled());
			PluginRefDesc.SupportedTargetPlatforms = Plugin->GetDescriptor().SupportedTargetPlatforms;

			PluginDesc.Plugins.Add(MoveTemp(PluginRefDesc));
		}
	}
	
	bool bSuccess = PluginDesc.Save(FilePath, ErrorMessage);
	if (!bSuccess)
	{
		UE_LOG(LogBlueprintCodeGen, Error, TEXT("Failed to generate the plugin description file: %s"), *ErrorMessage.ToString());
	}
	return bSuccess;
}

//------------------------------------------------------------------------------
static bool BlueprintNativeCodeGenUtilsImpl::GenerateModuleSourceFiles(const FBlueprintNativeCodeGenPaths& TargetPaths, bool bExcludeMonolithicEngineHeaders)
{
	FText FailureReason;

	TArray<FString> PchIncludes;
	if (!bExcludeMonolithicEngineHeaders)
	{
		PchIncludes.Add(EngineHeaderFile);
	}
	PchIncludes.Add(TEXT("GeneratedCodeHelpers.h"));
	PchIncludes.Add(TEXT("Blueprint/BlueprintSupport.h"));
	PchIncludes.Add(NativizedDependenciesFileName() + TEXT(".h"));

	TArray<FString> FilesToIncludeInModuleHeader;
	GConfig->GetArray(TEXT("BlueprintNativizationSettings"), TEXT("FilesToIncludeInModuleHeader"), FilesToIncludeInModuleHeader, GEditorIni);
	PchIncludes.Append(FilesToIncludeInModuleHeader);

	bool bSuccess = GameProjectUtils::GeneratePluginModuleHeaderFile(TargetPaths.RuntimeModuleFile(FBlueprintNativeCodeGenPaths::HFile), PchIncludes, FailureReason);

	if (bSuccess)
	{
		const FString NoStartupCode = TEXT("");
		bSuccess &= GameProjectUtils::GeneratePluginModuleCPPFile(TargetPaths.RuntimeModuleFile(FBlueprintNativeCodeGenPaths::CppFile),
			TargetPaths.RuntimeModuleName(), NoStartupCode, FailureReason);
	}

	if (!bSuccess)
	{
		UE_LOG(LogBlueprintCodeGen, Error, TEXT("Failed to generate module source files: %s"), *FailureReason.ToString());
	}
	return bSuccess;
}

//------------------------------------------------------------------------------
static bool BlueprintNativeCodeGenUtilsImpl::GenerateNativizedDependenciesSourceFiles(const FBlueprintNativeCodeGenPaths& TargetPaths, bool bExcludeMonolithicEngineHeaders)
{
	FText FailureReason;
	bool bSuccess = true;

	IBlueprintCompilerCppBackendModule& CodeGenBackend = (IBlueprintCompilerCppBackendModule&)IBlueprintCompilerCppBackendModule::Get();
	const FString BaseFilename = NativizedDependenciesFileName();

	{
		const FString HeaderFilePath = FPaths::Combine(*TargetPaths.RuntimeSourceDir(FBlueprintNativeCodeGenPaths::HFile), *BaseFilename) + TEXT(".h");
		const FString HeaderFileContent = CodeGenBackend.DependenciesGlobalMapHeaderCode();
		bSuccess &= GameProjectUtils::WriteOutputFile(HeaderFilePath, HeaderFileContent, FailureReason);
	}

	{
		const FString SourceFilePath = FPaths::Combine(*TargetPaths.RuntimeSourceDir(FBlueprintNativeCodeGenPaths::CppFile), *BaseFilename) + TEXT(".cpp");
		const FString SourceFileContent = CodeGenBackend.DependenciesGlobalMapBodyCode(bExcludeMonolithicEngineHeaders ? BaseFilename : TargetPaths.RuntimeModuleName());
		bSuccess &= GameProjectUtils::WriteOutputFile(SourceFilePath, SourceFileContent, FailureReason);
	}

	if (!bSuccess)
	{
		UE_LOG(LogBlueprintCodeGen, Error, TEXT("Failed to generate NativizedDependencies source files: %s"), *FailureReason.ToString());
	}
	return bSuccess;
}

//------------------------------------------------------------------------------
static bool BlueprintNativeCodeGenUtilsImpl::GenerateModuleBuildFile(const FBlueprintNativeCodeGenManifest& Manifest)
{
	FModuleManager& ModuleManager = FModuleManager::Get();

	// Gather the set of installed plugin modules
	TMap<FString, FString> ModuleToPluginMap;
	for (auto Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		for (auto PluginModule : Plugin->GetDescriptor().Modules)
		{
			ModuleToPluginMap.Add(PluginModule.Name.ToString(), Plugin->GetName());
		}
	}
	
	TArray<FString> PublicDependencies;
	// for IModuleInterface
	PublicDependencies.Add(CoreModuleName);
	if (!Manifest.GetCompilerNativizationOptions().bExcludeMonolithicHeaders)
	{
		// for Engine.h
		PublicDependencies.Add(EngineModuleName);
	}

	if (GameProjectUtils::ProjectHasCodeFiles()) 
	{
		const FString GameModuleName = FApp::GetProjectName();
		if (ModuleManager.ModuleExists(*GameModuleName))
		{
			PublicDependencies.Add(GameModuleName);
		}
	}

	auto IncludeAdditionalPublicDependencyModules = [&](const TCHAR* AdditionalPublicDependencyModuleSection)
	{
		TArray<FString> AdditionalPublicDependencyModuleNames;
		GConfig->GetArray(TEXT("BlueprintNativizationSettings"), AdditionalPublicDependencyModuleSection, AdditionalPublicDependencyModuleNames, GEditorIni);

		for (FString ModuleName : AdditionalPublicDependencyModuleNames)
		{
			if (const FString* PluginName = ModuleToPluginMap.Find(ModuleName))
			{
				PluginDependencies.Add(*PluginName);
			}
			
			PublicDependencies.Add(ModuleName);
		}
	};

	IncludeAdditionalPublicDependencyModules(TEXT("AdditionalPublicDependencyModuleNames"));
	if (Manifest.GetCompilerNativizationOptions().ServerOnlyPlatform) //or !ClientOnlyPlatform ?
	{
		IncludeAdditionalPublicDependencyModules(TEXT("AdditionalPublicDependencyModuleNamesServer"));
	}
	if (Manifest.GetCompilerNativizationOptions().ClientOnlyPlatform)
	{
		IncludeAdditionalPublicDependencyModules(TEXT("AdditionalPublicDependencyModuleNamesClient"));
	}

	TArray<FString> PrivateDependencies;

	const TArray<UPackage*>& ModulePackages = Manifest.GetModuleDependencies();
	PrivateDependencies.Reserve(ModulePackages.Num());

	for (UPackage* ModulePkg : ModulePackages)
	{
		const FString PkgModuleName = FPackageName::GetLongPackageAssetName(ModulePkg->GetName());
		if (ModuleManager.ModuleExists(*PkgModuleName))
		{
			if (Manifest.GetCompilerNativizationOptions().ExcludedModules.Contains(FName(*PkgModuleName)))
			{
				continue;
			}
			if (!PublicDependencies.Contains(PkgModuleName))
			{
				if (const FString* PluginName = ModuleToPluginMap.Find(PkgModuleName))
				{
					PluginDependencies.Add(*PluginName);
				}
			
				PrivateDependencies.Add(PkgModuleName);
			}
		}
		else
		{
			UE_LOG(LogBlueprintCodeGen, Warning, TEXT("Failed to find module for package: %s"), *PkgModuleName);
		}
	}
	FBlueprintNativeCodeGenPaths TargetPaths = Manifest.GetTargetPaths();
	
	FText ErrorMessage;
	bool bSuccess = GameProjectUtils::GeneratePluginModuleBuildFile(TargetPaths.RuntimeBuildFile(), TargetPaths.RuntimeModuleName(),
		PublicDependencies, PrivateDependencies, ErrorMessage, Manifest.GetCompilerNativizationOptions().bExcludeMonolithicHeaders);

	if (!bSuccess)
	{
		UE_LOG(LogBlueprintCodeGen, Error, TEXT("Failed to generate module build file: %s"), *ErrorMessage.ToString());
	}
	return bSuccess;
}

//------------------------------------------------------------------------------
static UClass* BlueprintNativeCodeGenUtilsImpl::ResolveReplacementType(const FConvertedAssetRecord& ConversionRecord)
{
	const UClass* AssetType = ConversionRecord.AssetType;
	check(AssetType != nullptr);

	if (AssetType->IsChildOf<UUserDefinedEnum>())
	{
		return UEnum::StaticClass();
	}
	else if (AssetType->IsChildOf<UUserDefinedStruct>())
	{
		return UScriptStruct::StaticClass();
	}
	else if (AssetType->IsChildOf<UBlueprint>())
	{
		return UDynamicClass::StaticClass();
	}
	else
	{
		UE_LOG(LogBlueprintCodeGen, Error, TEXT("Unsupported asset type (%s); cannot determine replacement type."), *AssetType->GetName());
	}
	return nullptr;
}

/*******************************************************************************
 * FBlueprintNativeCodeGenUtils
 ******************************************************************************/

//------------------------------------------------------------------------------
bool FBlueprintNativeCodeGenUtils::FinalizePlugin(const FBlueprintNativeCodeGenManifest& Manifest)
{
	bool bSuccess = true;
	const bool bExcludeMonolithicHeaders = Manifest.GetCompilerNativizationOptions().bExcludeMonolithicHeaders;
	FBlueprintNativeCodeGenPaths TargetPaths = Manifest.GetTargetPaths();
	bSuccess = bSuccess && BlueprintNativeCodeGenUtilsImpl::GenerateModuleBuildFile(Manifest);
	bSuccess = bSuccess && BlueprintNativeCodeGenUtilsImpl::GenerateModuleSourceFiles(TargetPaths, bExcludeMonolithicHeaders);
	bSuccess = bSuccess && BlueprintNativeCodeGenUtilsImpl::GenerateNativizedDependenciesSourceFiles(TargetPaths, bExcludeMonolithicHeaders);
	bSuccess = bSuccess && BlueprintNativeCodeGenUtilsImpl::GeneratePluginDescFile(TargetPaths);
	return bSuccess;
}

//------------------------------------------------------------------------------
void FBlueprintNativeCodeGenUtils::GenerateCppCode(UObject* Obj, TSharedPtr<FString> OutHeaderSource, TSharedPtr<FString> OutCppSource, TSharedPtr<FNativizationSummary> NativizationSummary, const FCompilerNativizationOptions& NativizationOptions)
{
	auto UDEnum = Cast<UUserDefinedEnum>(Obj);
	auto UDStruct = Cast<UUserDefinedStruct>(Obj);
	auto BPGC = Cast<UClass>(Obj);
	auto InBlueprintObj = BPGC ? Cast<UBlueprint>(BPGC->ClassGeneratedBy) : Cast<UBlueprint>(Obj);

	OutHeaderSource->Empty();
	OutCppSource->Empty();

	if (InBlueprintObj)
	{
		if (EBlueprintStatus::BS_Error == InBlueprintObj->Status)
		{
			UE_LOG(LogBlueprintCodeGen, Error, TEXT("Cannot convert \"%s\". It has errors."), *InBlueprintObj->GetPathName());
			return;
		}

		check(InBlueprintObj->GetOutermost() != GetTransientPackage());
		if (!ensureMsgf(InBlueprintObj->GeneratedClass, TEXT("Invalid generated class for %s"), *InBlueprintObj->GetName()))
		{
			return;
		}
		check(OutHeaderSource.IsValid());
		check(OutCppSource.IsValid());

		FDisableGatheringDataOnScope DisableFib;

		const FString TempPackageName = FString::Printf(TEXT("%s%s"), *UDynamicClass::GetTempPackagePrefix(), *InBlueprintObj->GetOutermost()->GetPathName());
		UPackage* TempPackage = CreatePackage(nullptr, *TempPackageName);
		check(TempPackage);
		ON_SCOPE_EXIT
		{
			TempPackage->RemoveFromRoot();
			TempPackage->MarkPendingKill();
		};

		TextNamespaceUtil::ForcePackageNamespace(TempPackage, TextNamespaceUtil::GetPackageNamespace(InBlueprintObj));

		UBlueprint* DuplicateBP = nullptr;
		{
			FBlueprintDuplicationScopeFlags BPDuplicationFlags(FBlueprintDuplicationScopeFlags::NoExtraCompilation 
				| FBlueprintDuplicationScopeFlags::TheSameTimelineGuid 
				| FBlueprintDuplicationScopeFlags::ValidatePinsUsingSourceClass
				| FBlueprintDuplicationScopeFlags::TheSameNodeGuid);
			DuplicateBP = DuplicateObject<UBlueprint>(InBlueprintObj, TempPackage, *InBlueprintObj->GetName());
		}
		ensure((nullptr != DuplicateBP->GeneratedClass) && (InBlueprintObj->GeneratedClass != DuplicateBP->GeneratedClass));
		ON_SCOPE_EXIT
		{
			DuplicateBP->RemoveFromRoot();
			DuplicateBP->MarkPendingKill();
		};

		IBlueprintCompilerCppBackendModule& CodeGenBackend = (IBlueprintCompilerCppBackendModule&)IBlueprintCompilerCppBackendModule::Get();
		CodeGenBackend.GetOriginalClassMap().Add(*DuplicateBP->GeneratedClass, *InBlueprintObj->GeneratedClass);
		CodeGenBackend.NativizationSummary() = NativizationSummary;

		{
			FBlueprintCompilationManager::CompileSynchronouslyToCpp( DuplicateBP, OutHeaderSource, OutCppSource, NativizationOptions );
			
			IKismetCompilerInterface& Compiler = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(KISMET_COMPILER_MODULENAME);
			Compiler.RemoveBlueprintGeneratedClasses(DuplicateBP);
		}

		if (EBlueprintType::BPTYPE_Interface == DuplicateBP->BlueprintType && OutCppSource.IsValid())
		{
			OutCppSource->Empty(); // ugly temp hack
		}
	}
	else if ((UDEnum || UDStruct) && OutHeaderSource.IsValid())
	{
		IKismetCompilerInterface& Compiler = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(KISMET_COMPILER_MODULENAME);
		if (UDEnum)
		{
			Compiler.GenerateCppCodeForEnum(UDEnum, NativizationOptions, *OutHeaderSource, *OutCppSource);
		}
		else if (UDStruct)
		{
			Compiler.GenerateCppCodeForStruct(UDStruct, NativizationOptions, *OutHeaderSource, *OutCppSource);
		}
	}
	else
	{
		ensure(false);
	}
}

/*******************************************************************************
 * FScopedFeedbackContext
 ******************************************************************************/

//------------------------------------------------------------------------------
FBlueprintNativeCodeGenUtils::FScopedFeedbackContext::FScopedFeedbackContext()
	: OldContext(GWarn)
	, ErrorCount(0)
	, WarningCount(0)
{
	TreatWarningsAsErrors = GWarn->TreatWarningsAsErrors;
	GWarn = this;
}

//------------------------------------------------------------------------------
FBlueprintNativeCodeGenUtils::FScopedFeedbackContext::~FScopedFeedbackContext()
{
	GWarn = OldContext;
}

//------------------------------------------------------------------------------
bool FBlueprintNativeCodeGenUtils::FScopedFeedbackContext::HasErrors()
{
	return (ErrorCount > 0) || (TreatWarningsAsErrors && (WarningCount > 0));
}

//------------------------------------------------------------------------------
void FBlueprintNativeCodeGenUtils::FScopedFeedbackContext::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Warning:
		{
			++WarningCount;
		} 
		break;

	case ELogVerbosity::Error:
	case ELogVerbosity::Fatal:
		{
			++ErrorCount;
		} 
		break;

	default:
		break;
	}

	OldContext->Serialize(V, Verbosity, Category);
}

//------------------------------------------------------------------------------
void FBlueprintNativeCodeGenUtils::FScopedFeedbackContext::Flush()
{
	WarningCount = ErrorCount = 0;
	OldContext->Flush();
}
