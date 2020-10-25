// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADTranslator.h"

#ifdef CAD_LIBRARY
#include "CoreTechParametricSurfaceExtension.h"
#include "DatasmithCADTranslatorModule.h"
#include "DatasmithDispatcher.h"
#include "DatasmithMeshBuilder.h"
#include "DatasmithSceneGraphBuilder.h"
#include "IDatasmithSceneElements.h"
#include "Misc/FileHelper.h"

static TAutoConsoleVariable<int32> CVarStaticCADTranslatorEnableThreadedImport(
	TEXT("r.CADTranslator.EnableThreadedImport"),
	1,
	TEXT("Activate to parallelise CAD file processing.\n"),
	ECVF_Default);

void FDatasmithCADTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
#ifndef CAD_TRANSLATOR_DEBUG
	OutCapabilities.bParallelLoadStaticMeshSupported = true;
#endif
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("CATPart"), TEXT("CATIA Part files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("CATProduct"), TEXT("CATIA Product files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("cgr"), TEXT("CATIA Graphical Representation V5 files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("3dxml"), TEXT("CATIA files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("3drep"), TEXT("CATIA files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("model"), TEXT("CATIA V4 files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("asm.*"), TEXT("Creo Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("asm"), TEXT("Creo, NX Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("creo.*"), TEXT("Creo Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("creo"), TEXT("Creo Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("neu"), TEXT("Creo Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("prt.*"), TEXT("Creo Part files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("prt"), TEXT("Creo, NX Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("iam"), TEXT("Inventor Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("ipt"), TEXT("Inventor Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("iges"), TEXT("IGES files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("igs"), TEXT("IGES files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("jt"), TEXT("JT Open files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("sat"), TEXT("3D ACIS model files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("SLDASM"), TEXT("SolidWorks Product files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("SLDPRT"), TEXT("SolidWorks Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("step"), TEXT("Step files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("stp"), TEXT("Step files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("x_t"), TEXT("Parasolid files (Text format)") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("x_b"), TEXT("Parasolid files (Binary format)") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("asm"), TEXT("Unigraphics Assembly, NX files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("prt"), TEXT("Unigraphics, NX Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("dwg"), TEXT("AutoCAD, Model files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("dgn"), TEXT("MicroStation files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("ct"), TEXT("Kernel_IO files") });
}

bool FDatasmithCADTranslator::LoadScene(TSharedRef<IDatasmithScene> DatasmithScene)
{
	ImportParameters.MetricUnit = 0.001;
	ImportParameters.ScaleFactor = 0.1;
	const FDatasmithTessellationOptions& TesselationOptions = GetCommonTessellationOptions();
	ImportParameters.ChordTolerance = TesselationOptions.ChordTolerance;
	ImportParameters.MaxEdgeLength = TesselationOptions.MaxEdgeLength;
	ImportParameters.MaxNormalAngle = TesselationOptions.NormalTolerance;
	ImportParameters.StitchingTechnique = (CADLibrary::EStitchingTechnique) TesselationOptions.StitchingTechnique;

	CADLibrary::FFileDescription FileDescription(*FPaths::ConvertRelativePathToFull(GetSource().GetSourceFile()),
		TEXT(""),
		*FPaths::GetPath(FPaths::ConvertRelativePathToFull(GetSource().GetSourceFile())) );

	if (FileDescription.Extension == TEXT("jt"))
	{
		ImportParameters.MetricUnit = 1.;
		ImportParameters.ScaleFactor = 100.;
	}

	ImportParameters.ModelCoordSys = CADLibrary::EModelCoordSystem::ZUp_RightHanded;
	if (FileDescription.Extension == TEXT("prt")) // NX
	{
		ImportParameters.ModelCoordSys = CADLibrary::EModelCoordSystem::ZUp_RightHanded;
		ImportParameters.DisplayPreference = CADLibrary::EDisplayPreference::ColorOnly;
		ImportParameters.Propagation = CADLibrary::EDisplayDataPropagationMode::BodyOnly;
	}
	else if (FileDescription.Extension == TEXT("sldprt") || FileDescription.Extension == TEXT("sldasm") || // Solidworks
		FileDescription.Extension == TEXT("iam") || FileDescription.Extension == TEXT("ipt") || // Inventor
		FileDescription.Extension.StartsWith(TEXT("asm")) || FileDescription.Extension.StartsWith(TEXT("creo")) || FileDescription.Extension.StartsWith(TEXT("prt")) // Creo
		)
	{
		ImportParameters.ModelCoordSys = CADLibrary::EModelCoordSystem::YUp_RightHanded;
		ImportParameters.DisplayPreference = CADLibrary::EDisplayPreference::ColorOnly;
		ImportParameters.Propagation = CADLibrary::EDisplayDataPropagationMode::BodyOnly;
	}

	FString CachePath = FPaths::ConvertRelativePathToFull(FDatasmithCADTranslatorModule::Get().GetCacheDir());

	TMap<uint32, FString> CADFileToUE4FileMap;
	int32 NumCores = FPlatformMisc::NumberOfCores();
	{
		DatasmithDispatcher::FDatasmithDispatcher Dispatcher(ImportParameters, CachePath, NumCores, CADFileToUE4FileMap, CADFileToUE4GeomMap);
		Dispatcher.AddTask(FileDescription);

		bool bWithProcessor = (CVarStaticCADTranslatorEnableThreadedImport.GetValueOnAnyThread() != 0);

#ifdef CAD_TRANSLATOR_DEBUG
		bWithProcessor = false;
#endif //CAD_TRANSLATOR_DEBUG

		Dispatcher.Process(bWithProcessor);
	}

	FDatasmithSceneGraphBuilder SceneGraphBuilder(CADFileToUE4FileMap, CachePath, DatasmithScene, GetSource(), ImportParameters);
	SceneGraphBuilder.Build();

	MeshBuilderPtr = MakeUnique<FDatasmithMeshBuilder>(CADFileToUE4GeomMap, CachePath, ImportParameters);

	return true;
}

void FDatasmithCADTranslator::UnloadScene()
{
	MeshBuilderPtr = nullptr;

	CADFileToUE4GeomMap.Empty();
}

bool FDatasmithCADTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
{
	if (!MeshBuilderPtr.IsValid())
	{
		return false;
	}

	CADLibrary::FMeshParameters MeshParameters;

	if (TOptional< FMeshDescription > Mesh = MeshBuilderPtr->GetMeshDescription(MeshElement, MeshParameters))
	{
		OutMeshPayload.LodMeshes.Add(MoveTemp(Mesh.GetValue()));

		DatasmithCoreTechParametricSurfaceData::AddCoreTechSurfaceDataForMesh(MeshElement, ImportParameters, MeshParameters, GetCommonTessellationOptions(), OutMeshPayload);
	}
	return OutMeshPayload.LodMeshes.Num() > 0;
}

void FDatasmithCADTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	FDatasmithCoreTechTranslator::SetSceneImportOptions(Options);
}
#endif




