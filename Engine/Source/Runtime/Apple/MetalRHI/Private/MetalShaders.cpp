// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalShaders.cpp: Metal shader RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "MetalShaderResources.h"
#include "MetalResources.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/Compression.h"
#include "Misc/MessageDialog.h"

// The Metal standard library extensions we need for UE4.
#include "ue4_stdlib.h"

#define SHADERCOMPILERCOMMON_API
#	include "Developer/ShaderCompilerCommon/Public/ShaderCompilerCommon.h"
#undef SHADERCOMPILERCOMMON_API

/** Set to 1 to enable shader debugging (makes the driver save the shader source) */
#define DEBUG_METAL_SHADERS (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)

static FString METAL_LIB_EXTENSION(TEXT(".metallib"));
static FString METAL_MAP_EXTENSION(TEXT(".metalmap"));

struct FMetalCompiledShaderKey
{
	FMetalCompiledShaderKey(
		uint32 InCodeSize,
		uint32 InCodeCRC,
		uint32 InConstants
		)
		: CodeSize(InCodeSize)
		, CodeCRC(InCodeCRC)
		, Constants(InConstants)
	{}

	friend bool operator ==(const FMetalCompiledShaderKey& A, const FMetalCompiledShaderKey& B)
	{
		return A.CodeSize == B.CodeSize && A.CodeCRC == B.CodeCRC && A.Constants == B.Constants;
	}

	friend uint32 GetTypeHash(const FMetalCompiledShaderKey &Key)
	{
		return HashCombine(HashCombine(GetTypeHash(Key.CodeSize), GetTypeHash(Key.CodeCRC)), GetTypeHash(Key.Constants));
	}

	uint32 CodeSize;
	uint32 CodeCRC;
	uint32 Constants;
};

struct FMetalCompiledShaderCache
{
public:
	FMetalCompiledShaderCache()
	{
	}
	
	~FMetalCompiledShaderCache()
	{
	}
	
	mtlpp::Function FindRef(FMetalCompiledShaderKey const& Key)
	{
		FRWScopeLock ScopedLock(Lock, SLT_ReadOnly);
		mtlpp::Function Func = Cache.FindRef(Key);
		return Func;
	}
	
	mtlpp::Library FindLibrary(mtlpp::Function const& Function)
	{
		FRWScopeLock ScopedLock(Lock, SLT_ReadOnly);
		mtlpp::Library Lib = LibCache.FindRef(Function.GetPtr());
		return Lib;
	}
	
	void Add(FMetalCompiledShaderKey Key, mtlpp::Library const& Lib, mtlpp::Function const& Function)
	{
		FRWScopeLock ScopedLock(Lock, SLT_Write);
		if (Cache.FindRef(Key) == nil)
		{
			Cache.Add(Key, Function);
			LibCache.Add(Function.GetPtr(), Lib);
		}
	}
	
private:
	FRWLock Lock;
	TMap<FMetalCompiledShaderKey, mtlpp::Function> Cache;
	TMap<mtlpp::Function::Type, mtlpp::Library> LibCache;
};

static FMetalCompiledShaderCache& GetMetalCompiledShaderCache()
{
	static FMetalCompiledShaderCache CompiledShaderCache;
	return CompiledShaderCache;
}

#if !UE_BUILD_SHIPPING

struct FMetalShaderDebugCache
{
	static FMetalShaderDebugCache& Get()
	{
		static FMetalShaderDebugCache sSelf;
		return sSelf;
	}
	
	class FMetalShaderDebugZipFile* GetDebugFile(FString Path);
	ns::String GetShaderCode(uint32 ShaderSrcLen, uint32 ShaderSrcCRC);
	
	FCriticalSection Mutex;
	TMap<FString, class FMetalShaderDebugZipFile*> DebugFiles;
};

class FMetalShaderDebugZipFile
{
	struct FFileEntry
	{
		FString Filename;
		uint32 Crc32;
		uint64 Length;
		uint64 Offset;
		uint32 Time;
		
		FFileEntry(const FString& InFilename, uint32 InCrc32, uint64 InLength, uint64 InOffset, uint32 InTime)
		: Filename(InFilename)
		, Crc32(InCrc32)
		, Length(InLength)
		, Offset(InOffset)
		, Time(InTime)
		{}
	};
public:
	FMetalShaderDebugZipFile(FString LibPath)
	: File(nullptr)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		File = PlatformFile.OpenRead(*LibPath);
		if(File)
		{
			int64 SeekEndOffset = -1;
			
			// Write normal end of central directory record
			const static uint8 EndRecord[] =
			{
				0x50, 0x4b, 0x05, 0x06, 0xff, 0xff, 0xff, 0xff,
				0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
				0xff, 0xff, 0xff, 0xff, 0x00, 0x00
			};
			
			SeekEndOffset += sizeof(EndRecord);
			bool bOK = File->SeekFromEnd(-SeekEndOffset);
			if (bOK)
			{
				TArray<uint8> Data;
				Data.AddZeroed(sizeof(EndRecord));
				bOK = File->Read(Data.GetData(), sizeof(EndRecord));
				if (bOK)
				{
					bOK = (FMemory::Memcmp(Data.GetData(), EndRecord, sizeof(EndRecord)) == 0);
				}
			}
			
			// Write ZIP64 end of central directory locator
			const static uint8 Locator[] =
			{
				0x50, 0x4b, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00,
			};
			uint64 DirEndOffset = 0;
			if (bOK)
			{
				SeekEndOffset += sizeof(Locator) + sizeof(uint64) + sizeof(uint32);
				bOK = File->SeekFromEnd(-SeekEndOffset);
				if (bOK)
				{
					TArray<uint8> Data;
					Data.AddZeroed(sizeof(Locator));
					bOK = File->Read(Data.GetData(), sizeof(Locator));
					if (bOK)
					{
						bOK = (FMemory::Memcmp(Data.GetData(), Locator, sizeof(Locator)) == 0);
					}
					if (bOK)
					{
						bOK = File->Read((uint8*)&DirEndOffset, sizeof(uint64));
					}
				}
			}
			
			// Write ZIP64 end of central directory record
			const static uint8 Record[] =
			{
				0x50, 0x4b, 0x06, 0x06, 0x2c, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x2d, 0x00, 0x2d, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			};
			struct FMetalZipRecordData
			{
				uint64 FilesNum;
				uint64 FilesNum2;
				uint64 DirectorySizeInBytes;
				uint64 DirStartOffset;
			} RecordData;
			if (bOK)
			{
				SeekEndOffset += sizeof(Record) + (sizeof(uint64) * 4);
				bOK = File->SeekFromEnd(-SeekEndOffset);
				if (bOK)
				{
					TArray<uint8> Data;
					Data.AddZeroed(sizeof(Record));
					bOK = File->Read(Data.GetData(), sizeof(Record));
					if (bOK)
					{
						bOK = (FMemory::Memcmp(Data.GetData(), Record, sizeof(Record)) == 0);
					}
					if (bOK)
					{
						bOK = File->Read((uint8*)&RecordData, sizeof(RecordData));
					}
				}
			}
			
			if (bOK)
			{
				bOK = File->Seek(RecordData.DirStartOffset);
				if (bOK)
				{
					const static uint8 Footer[] =
					{
						0x50, 0x4b, 0x01, 0x02, 0x3f, 0x00, 0x2d, 0x00,
						0x00, 0x00, 0x00, 0x00
					};
					const static uint8 Fields[] =
					{
						0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						0x20, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
					};
					
					TArray<uint8> FooterData;
					FooterData.AddZeroed(sizeof(Footer));
					TArray<uint8> FieldsData;
					FieldsData.AddZeroed(sizeof(Fields));
					
					struct FMetalZipFileHeader {
						uint32 Time;
						uint32 CRC;
						uint64 SizeMarker;
						uint16 FilenameLen;
					} __attribute__((packed)) Header;
					
					struct FMetalZipFileTrailer {
						uint16 Flags;
						uint16 Attribs;
						uint64 CompressedLen;
						uint64 UncompressedLen;
						uint64 Offset;
						uint32 DiskNum;
					} __attribute__((packed)) Trailer;
					
					static const uint8 FileHeader[] =
					{
						0x50, 0x4b, 0x03, 0x04, 0x2d, 0x00, 0x00, 0x00,
						0x00, 0x00
					};
					
					uint32 FileHeaderFixedSize = sizeof(FileHeader) + sizeof(FMetalZipFileHeader) + sizeof(uint16) + sizeof(FMetalZipFileTrailer);
					
					FString Filename;
					
					while (bOK && Files.Num() < RecordData.FilesNum && File->Tell() < RecordData.DirStartOffset + RecordData.DirectorySizeInBytes)
					{
						bOK = File->Read(FooterData.GetData(), sizeof(Footer));
						if (bOK)
						{
							bOK = (FMemory::Memcmp(FooterData.GetData(), Footer, sizeof(Footer)) == 0);
						}
						
						if (bOK)
						{
							bOK = File->Read((uint8*)&Header, sizeof(Header));
							if (bOK)
							{
								bOK = (Header.SizeMarker == (uint64)0xffffffffffffffff);
							}
						}
						
						if (bOK)
						{
							bOK = File->Read(FieldsData.GetData(), sizeof(Fields));
							if (bOK)
							{
								bOK = (FMemory::Memcmp(FieldsData.GetData(), Fields, sizeof(Fields)) == 0);
							}
						}
						
						if (bOK)
						{
							TArray<uint8> FilenameData;
							FilenameData.AddZeroed(Header.FilenameLen+1);
							bOK = File->Read(FilenameData.GetData(), Header.FilenameLen);
							if (bOK)
							{
								Filename = UTF8_TO_TCHAR((char const*)FilenameData.GetData());
							}
						}
						
						if (bOK)
						{
							bOK = File->Read((uint8*)&Trailer, sizeof(Trailer));
							if (bOK)
							{
								bOK = (Trailer.Flags == (uint16)0x01 && Trailer.Attribs == (uint16)0x1c && Trailer.DiskNum == 0);
							}
						}
						
						if (bOK)
						{
							FFileEntry NewEntry(Filename, Header.CRC, Trailer.UncompressedLen, Trailer.Offset + FileHeaderFixedSize + Header.FilenameLen, Header.Time);
							Files.Add(NewEntry);
						}
					}
				}
			}
		}
	}

	~FMetalShaderDebugZipFile()
	{
		delete File;
	}
	
	ns::String GetShaderCode(uint32 ShaderSrcLen, uint32 ShaderSrcCRC)
	{
		ns::String Source;
		FScopeLock Lock(&Mutex);
		FString Name = FString::Printf(TEXT("%u_%u.metal"), ShaderSrcLen, ShaderSrcCRC);
		for (auto const& Entry : Files)
		{
			if (FPaths::GetCleanFilename(Entry.Filename) == Name)
			{
				if (File->Seek(Entry.Offset))
				{
					TArray<uint8> Data;
					Data.AddZeroed(Entry.Length+1);
					if (File->Read(Data.GetData(), Entry.Length))
					{
						Source = [NSString stringWithUTF8String:(char const*)Data.GetData()];
					}
				}
				
				break;
			}
		}
		return Source;
	}
	
private:
	FCriticalSection Mutex;
	IFileHandle* File;
	TArray<FFileEntry> Files;
};


FMetalShaderDebugZipFile* FMetalShaderDebugCache::GetDebugFile(FString Path)
{
	FScopeLock Lock(&Mutex);
	FMetalShaderDebugZipFile* Ref = DebugFiles.FindRef(Path);
	if (!Ref)
	{
		Ref = new FMetalShaderDebugZipFile(Path);
		DebugFiles.Add(Path, Ref);
	}
	return Ref;
}

ns::String FMetalShaderDebugCache::GetShaderCode(uint32 ShaderSrcLen, uint32 ShaderSrcCRC)
{
	ns::String Code;
	FScopeLock Lock(&Mutex);
	for (auto const& Ref : DebugFiles)
	{
		Code = Ref.Value->GetShaderCode(ShaderSrcLen, ShaderSrcCRC);
		if (Code)
		{
			break;
		}
	}
	return Code;
}

#endif

NSString* DecodeMetalSourceCode(uint32 CodeSize, TArray<uint8> const& CompressedSource)
{
	NSString* GlslCodeNSString = nil;
	if (CodeSize && CompressedSource.Num())
	{
		TArray<ANSICHAR> UncompressedCode;
		UncompressedCode.AddZeroed(CodeSize+1);
		bool bSucceed = FCompression::UncompressMemory(NAME_Zlib, UncompressedCode.GetData(), CodeSize, CompressedSource.GetData(), CompressedSource.Num());
		if (bSucceed)
		{
			GlslCodeNSString = [[NSString stringWithUTF8String:UncompressedCode.GetData()] retain];
		}
	}
	return GlslCodeNSString;
}

mtlpp::LanguageVersion ValidateVersion(uint8 Version)
{
	static uint32 MetalMacOSVersions[][3] = {
		{10,11,6},
		{10,11,6},
		{10,12,6},
		{10,13,0},
		{10,14,0},
	};
	static uint32 MetaliOSVersions[][3] = {
		{8,0,0},
		{9,0,0},
		{10,0,0},
		{11,0,0},
		{12,0,0},
	};
	static TCHAR const* StandardNames[] =
	{
		TEXT("Metal 1.0"),
		TEXT("Metal 1.1"),
		TEXT("Metal 1.2"),
		TEXT("Metal 2.0"),
		TEXT("Metal 2.1"),
	};
	
	mtlpp::LanguageVersion Result = mtlpp::LanguageVersion::Version1_1;
	switch(Version)
	{
		case 4:
			Result = mtlpp::LanguageVersion::Version2_1;
			break;
		case 3:
			Result = mtlpp::LanguageVersion::Version2_0;
			break;
		case 2:
			Result = mtlpp::LanguageVersion::Version1_2;
			break;
		case 1:
			Result = mtlpp::LanguageVersion::Version1_1;
			break;
		case 0:
		default:
#if PLATFORM_MAC
			Result = mtlpp::LanguageVersion::Version1_1;
#else
			Result = mtlpp::LanguageVersion::Version1_0;
#endif
			break;
	}
	
	if (!FApplePlatformMisc::IsOSAtLeastVersion(MetalMacOSVersions[Version], MetaliOSVersions[Version], MetaliOSVersions[Version]))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ShaderVersion"), FText::FromString(FString(StandardNames[Version])));
#if PLATFORM_MAC
		Args.Add(TEXT("RequiredOS"), FText::FromString(FString::Printf(TEXT("macOS %d.%d.%d"), MetalMacOSVersions[Version][0], MetalMacOSVersions[Version][1], MetalMacOSVersions[Version][2])));
#else
		Args.Add(TEXT("RequiredOS"), FText::FromString(FString::Printf(TEXT("%d.%d.%d"), MetaliOSVersions[Version][0], MetaliOSVersions[Version][1], MetaliOSVersions[Version][2])));
#endif
		FText LocalizedMsg = FText::Format(NSLOCTEXT("MetalRHI", "ShaderVersionUnsupported", "The current OS version does not support {ShaderVersion} required by the project. You must upgrade to {RequiredOS} to run this project."),Args);
		
		FText Title = NSLOCTEXT("MetalRHI", "ShaderVersionUnsupportedTitle", "Shader Version Unsupported");
		FMessageDialog::Open(EAppMsgType::Ok, LocalizedMsg, &Title);
		
		FPlatformMisc::RequestExit(true);
	}
	
	return Result;
}

/** Initialization constructor. */
template<typename BaseResourceType, int32 ShaderType>
void TMetalBaseShader<BaseResourceType, ShaderType>::Init(const TArray<uint8>& InShaderCode, FMetalCodeHeader& Header, mtlpp::Library InLibrary)
{
	FShaderCodeReader ShaderCode(InShaderCode);

	FMemoryReader Ar(InShaderCode, true);
	
	Ar.SetLimitSize(ShaderCode.GetActualShaderCodeSize());

	// was the shader already compiled offline?
	uint8 OfflineCompiledFlag;
	Ar << OfflineCompiledFlag;
	check(OfflineCompiledFlag == 0 || OfflineCompiledFlag == 1);

	// get the header
	Ar << Header;
	
	ValidateVersion(Header.Version);
	
	SourceLen = Header.SourceLen;
    SourceCRC = Header.SourceCRC;
	
	// If this triggers than a level above us has failed to provide valid shader data and the cook is probably bogus
	UE_CLOG(Header.SourceLen == 0 || Header.SourceCRC == 0, LogMetal, Fatal, TEXT("Invalid Shader Bytecode provided."));
	
    bDeviceFunctionConstants = Header.bDeviceFunctionConstants;

	// remember where the header ended and code (precompiled or source) begins
	int32 CodeOffset = Ar.Tell();
	uint32 BufferSize = ShaderCode.GetActualShaderCodeSize() - CodeOffset;
	const ANSICHAR* SourceCode = (ANSICHAR*)InShaderCode.GetData() + CodeOffset;

	// Only archived shaders should be in here.
	UE_CLOG(InLibrary && !(Header.CompileFlags & (1 << CFLAG_Archive)), LogMetal, Warning, TEXT("Shader being loaded wasn't marked for archiving but a MTLLibrary was provided - this is unsupported."));

	if (!OfflineCompiledFlag)
	{
		UE_LOG(LogMetal, Display, TEXT("Loaded a text shader (will be slower to load)"));
	}
	
	bool bOfflineCompile = (OfflineCompiledFlag > 0);
	
	const ANSICHAR* ShaderSource = ShaderCode.FindOptionalData('c');
	bool bHasShaderSource = (ShaderSource && FCStringAnsi::Strlen(ShaderSource) > 0);
    
    static bool bForceTextShaders = FMetalCommandQueue::SupportsFeature(EMetalFeaturesGPUTrace);
    if (!bHasShaderSource)
    {
        int32 LZMASourceSize = 0;
        int32 SourceSize = 0;
        const uint8* LZMASource = ShaderCode.FindOptionalDataAndSize('z', LZMASourceSize);
        const uint8* UnSourceLen = ShaderCode.FindOptionalDataAndSize('u', SourceSize);
        if (LZMASource && LZMASourceSize > 0 && UnSourceLen && SourceSize == sizeof(uint32))
        {
            CompressedSource.Append(LZMASource, LZMASourceSize);
            memcpy(&CodeSize, UnSourceLen, sizeof(uint32));
			bHasShaderSource = false;
        }
#if !UE_BUILD_SHIPPING
		else if(bForceTextShaders)
		{
			GlslCodeNSString = [FMetalShaderDebugCache::Get().GetShaderCode(SourceLen, SourceCRC).GetPtr() retain];
		}
#endif
        if (bForceTextShaders)
        {
            bHasShaderSource = (GetSourceCode() != nil);
        }
    }
    else if (bOfflineCompile && bHasShaderSource)
	{
		GlslCodeNSString = [NSString stringWithUTF8String:ShaderSource];
		check(GlslCodeNSString);
		[GlslCodeNSString retain];
	}
	
	bHasFunctionConstants = (Header.bDeviceFunctionConstants);

	ConstantValueHash = 0;
	
	Library = InLibrary;
	
	bool bNeedsCompiling = false;

	// Find the existing compiled shader in the cache.
	uint32 FunctionConstantHash = ConstantValueHash;
	FMetalCompiledShaderKey Key(Header.SourceLen, Header.SourceCRC, FunctionConstantHash);
	
	Function = GetMetalCompiledShaderCache().FindRef(Key);
	if (!Library && Function)
	{
		Library = GetMetalCompiledShaderCache().FindLibrary(Function);
	}
	else
	{
		bNeedsCompiling = true;
	}
	
    Bindings = Header.Bindings;
	if (bNeedsCompiling || !Library)
	{
		if (bOfflineCompile && bHasShaderSource)
		{
			// For debug/dev/test builds we can use the stored code for debugging - but shipping builds shouldn't have this as it is inappropriate.
	#if METAL_DEBUG_OPTIONS
			// For iOS/tvOS we must use runtime compilation to make the shaders debuggable, but
			bool bSavedSource = false;
			
	#if PLATFORM_MAC
			const ANSICHAR* ShaderPath = ShaderCode.FindOptionalData('p');
			bool const bHasShaderPath = (ShaderPath && FCStringAnsi::Strlen(ShaderPath) > 0);
			
			// on Mac if we have a path for the shader we can access the shader code
			if (bHasShaderPath && !bForceTextShaders && (GetSourceCode() != nil))
			{
				FString ShaderPathString(ShaderPath);
				
				if (IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShaderPathString), true))
				{
					FString Source(GetSourceCode());
					bSavedSource = FFileHelper::SaveStringToFile(Source, *ShaderPathString);
				}
				
				static bool bAttemptedAuth = false;
				if (!bSavedSource && !bAttemptedAuth)
				{
					bAttemptedAuth = true;
					
					if (IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShaderPathString), true))
					{
						bSavedSource = FFileHelper::SaveStringToFile(FString(GlslCodeNSString), *ShaderPathString);
					}
					
					if (!bSavedSource)
					{
						FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
													 *NSLOCTEXT("MetalRHI", "ShaderDebugAuthFail", "Could not access directory required for debugging optimised Metal shaders. Falling back to slower runtime compilation of shaders for debugging.").ToString(), TEXT("Error"));
					}
				}
			}
	#endif
			// Switch the compile mode so we get debuggable shaders even if we failed to save - if we didn't want
			// shader debugging we wouldn't have included the code...
			bOfflineCompile = bSavedSource || (bOfflineCompile && !bForceTextShaders);
	#endif
		}
		
		if (bOfflineCompile METAL_DEBUG_OPTION(&& !(bHasShaderSource && bForceTextShaders)))
		{
			if (InLibrary)
			{
				Library = InLibrary;
			}
			else
			{
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewLibraryBinary: %d_%d"), SourceLen, SourceCRC)));
				
				// Archived shaders should never get in here.
				check(!(Header.CompileFlags & (1 << CFLAG_Archive)) || BufferSize > 0);
				
				// allow GCD to copy the data into its own buffer
				//		dispatch_data_t GCDBuffer = dispatch_data_create(InShaderCode.GetTypedData() + CodeOffset, ShaderCode.GetActualShaderCodeSize() - CodeOffset, nil, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
				ns::AutoReleasedError AError;
				void* Buffer = FMemory::Malloc( BufferSize );
				FMemory::Memcpy( Buffer, InShaderCode.GetData() + CodeOffset, BufferSize );
				dispatch_data_t GCDBuffer = dispatch_data_create(Buffer, BufferSize, dispatch_get_main_queue(), ^(void) { FMemory::Free(Buffer); } );
				
				// load up the already compiled shader
				Library = GetMetalDeviceContext().GetDevice().NewLibrary(GCDBuffer, &AError);
				dispatch_release(GCDBuffer);
				
                if (Library == nil)
                {
					NSLog(@"Failed to create library: %@", ns::Error(AError).GetPtr());
                }
			}
		}
		else
		{
			METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewLibrarySource: %d_%d"), SourceLen, SourceCRC)));
			NSString* ShaderString = ((OfflineCompiledFlag == 0) ? [NSString stringWithUTF8String:SourceCode] : GlslCodeNSString);
			
			if(Header.ShaderName.Len())
			{
				ShaderString = [NSString stringWithFormat:@"// %@\n%@", Header.ShaderName.GetNSString(), ShaderString];
			}
			
			static NSString* UE4StdLibString = [[NSString alloc] initWithBytes:ue4_stdlib_metal length:ue4_stdlib_metal_len encoding:NSUTF8StringEncoding];
			
			NSString* NewShaderString = [ShaderString stringByReplacingOccurrencesOfString:@"#include \"ue4_stdlib.metal\"" withString:UE4StdLibString];
			NewShaderString = [NewShaderString stringByReplacingOccurrencesOfString:@"#pragma once" withString:@""];
			
			mtlpp::CompileOptions CompileOptions;
			
#if DEBUG_METAL_SHADERS
			static bool bForceFastMath = FParse::Param(FCommandLine::Get(), TEXT("metalfastmath"));
			static bool bForceNoFastMath = FParse::Param(FCommandLine::Get(), TEXT("metalnofastmath"));
			if (bForceNoFastMath)
			{
				CompileOptions.SetFastMathEnabled(NO);
			}
			else if (bForceFastMath)
			{
				CompileOptions.SetFastMathEnabled(YES);
			}
			else
#endif
			{
				CompileOptions.SetFastMathEnabled((BOOL)(!(Header.CompileFlags & (1 << CFLAG_NoFastMath))));
			}
			
#if !PLATFORM_MAC || DEBUG_METAL_SHADERS
			NSMutableDictionary *PreprocessorMacros = [NSMutableDictionary new];
#if !PLATFORM_MAC // Pretty sure that as_type-casts work on macOS, but they don't for half2<->uint on older versions of the iOS runtime compiler.
			[PreprocessorMacros addEntriesFromDictionary: @{ @"METAL_RUNTIME_COMPILER" : @(1)}];
#endif
#if DEBUG_METAL_SHADERS
			[PreprocessorMacros addEntriesFromDictionary: @{ @"MTLSL_ENABLE_DEBUG_INFO" : @(1)}];
#endif
			CompileOptions.SetPreprocessorMacros(PreprocessorMacros);
#endif
			
			mtlpp::LanguageVersion MetalVersion;
			switch(Header.Version)
			{
				case 6:
				case 5:
				case 4:
					MetalVersion = mtlpp::LanguageVersion::Version2_1;
					break;
				case 3:
					MetalVersion = mtlpp::LanguageVersion::Version2_0;
					break;
				case 2:
					MetalVersion = mtlpp::LanguageVersion::Version1_2;
					break;
				case 1:
					MetalVersion = mtlpp::LanguageVersion::Version1_1;
					break;
				case 0:
	#if PLATFORM_MAC
					MetalVersion = mtlpp::LanguageVersion::Version1_1;
	#else
					MetalVersion = mtlpp::LanguageVersion::Version1_0;
	#endif
					break;
				default:
					UE_LOG(LogRHI, Fatal, TEXT("Failed to create shader with unknown version %d: %s"), Header.Version, *FString(NewShaderString));
	#if PLATFORM_MAC
					MetalVersion = mtlpp::LanguageVersion::Version1_1;
	#else
					MetalVersion = mtlpp::LanguageVersion::Version1_0;
	#endif
					break;
				}
			CompileOptions.SetLanguageVersion(MetalVersion);
			
			ns::AutoReleasedError Error;
			Library = GetMetalDeviceContext().GetDevice().NewLibrary(NewShaderString, CompileOptions, &Error);
			if (Library == nil)
			{
				UE_LOG(LogRHI, Error, TEXT("*********** Error\n%s"), *FString(NewShaderString));
				UE_LOG(LogRHI, Fatal, TEXT("Failed to create shader: %s"), *FString([Error.GetPtr() description]));
			}
			else if (Error != nil)
			{
				// Warning...
				UE_LOG(LogRHI, Warning, TEXT("*********** Warning\n%s"), *FString(NewShaderString));
				UE_LOG(LogRHI, Warning, TEXT("Created shader with warnings: %s"), *FString([Error.GetPtr() description]));
			}
			
			GlslCodeNSString = NewShaderString;
			[GlslCodeNSString retain];
		}
		
        GetCompiledFunction(true);
    }
	UniformBuffersCopyInfo = Header.UniformBuffersCopyInfo;
	SideTableBinding = Header.SideTable;
}

/** Destructor */
template<typename BaseResourceType, int32 ShaderType>
TMetalBaseShader<BaseResourceType, ShaderType>::~TMetalBaseShader()
{
	[GlslCodeNSString release];
}

template<typename BaseResourceType, int32 ShaderType>
mtlpp::Function TMetalBaseShader<BaseResourceType, ShaderType>::GetCompiledFunction( bool const bAsync)
{
	mtlpp::Function Func = Function;
    
	if (!Func)
	{
		// Find the existing compiled shader in the cache.
		uint32 FunctionConstantHash = ConstantValueHash;
		FMetalCompiledShaderKey Key(SourceLen, SourceCRC, FunctionConstantHash);
		Func = Function = GetMetalCompiledShaderCache().FindRef(Key);
		
		if (!Func)
		{
			// Get the function from the library - the function name is "Main" followed by the CRC32 of the source MTLSL as 0-padded hex.
			// This ensures that even if we move to a unified library that the function names will be unique - duplicates will only have one entry in the library.
			NSString* Name = [NSString stringWithFormat:@"Main_%0.8x_%0.8x", SourceLen, SourceCRC];
			mtlpp::FunctionConstantValues ConstantValues(nil);
            if (bHasFunctionConstants)
            {
                ConstantValues = mtlpp::FunctionConstantValues();
				
                if (bDeviceFunctionConstants)
                {
                    // Index 33 is the device vendor id constant
					ConstantValues.SetConstantValue(&GRHIVendorId, mtlpp::DataType::UInt, @"GMetalDeviceManufacturer");
                }
            }
            
            if (!bHasFunctionConstants || !bAsync)
            {
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewFunction: %s"), *FString(Name))));
                if (!bHasFunctionConstants)
                {
                    Function = Library.NewFunction(Name);
                }
                else
                {
					ns::AutoReleasedError AError;
					Function = Library.NewFunction(Name, ConstantValues, &AError);
					ns::Error Error = AError;
					UE_CLOG(Function == nil, LogMetal, Error, TEXT("Failed to create function: %s"), *FString(Error.GetPtr().description));
                    UE_CLOG(Function == nil, LogMetal, Fatal, TEXT("*********** Error\n%s"), *FString(GetSourceCode()));
                }
                
                check(Function);
                GetMetalCompiledShaderCache().Add(Key, Library, Function);
                
                Func = Function;
            }
            else
            {
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewFunctionAsync: %s"), *FString(Name))));
				METAL_GPUPROFILE(uint64 CPUStart = CPUStat.Stats ? CPUStat.Stats->CPUStartTime : 0);
#if ENABLE_METAL_GPUPROFILE
                ns::String nsName(Name);
				Library.NewFunction(Name, ConstantValues, [Key, this, CPUStart, nsName](mtlpp::Function const& NewFunction, ns::Error const& Error){
#else
				Library.NewFunction(Name, ConstantValues, [Key, this](mtlpp::Function const& NewFunction, ns::Error const& Error){
#endif
					METAL_GPUPROFILE(FScopedMetalCPUStats CompletionStat(FString::Printf(TEXT("NewFunctionCompletion: %s"), *FString(nsName.GetPtr()))));
					UE_CLOG(NewFunction == nil, LogMetal, Error, TEXT("Failed to create function: %s"), *FString(Error.GetPtr().description));
					UE_CLOG(NewFunction == nil, LogMetal, Fatal, TEXT("*********** Error\n%s"), *FString(GetSourceCode()));
					
					GetMetalCompiledShaderCache().Add(Key, Library, NewFunction);
#if ENABLE_METAL_GPUPROFILE
					if (CompletionStat.Stats)
					{
						CompletionStat.Stats->CPUStartTime = CPUStart;
					}
#endif
				});

                return nil;
            }
		}
	}	
	
    check(Func);
	return Func;
}

FMetalComputeShader::FMetalComputeShader(const TArray<uint8>& InCode, mtlpp::Library InLibrary)
: NumThreadsX(0)
, NumThreadsY(0)
, NumThreadsZ(0)
{
    Pipeline = nil;
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
	
	NumThreadsX = FMath::Max((int32)Header.NumThreadsX, 1);
	NumThreadsY = FMath::Max((int32)Header.NumThreadsY, 1);
	NumThreadsZ = FMath::Max((int32)Header.NumThreadsZ, 1);
}

FMetalComputeShader::~FMetalComputeShader()
{
	[Pipeline release];
	Pipeline = nil;
}

FMetalShaderPipeline* FMetalComputeShader::GetPipeline()
{
	if (!Pipeline)
	{
		mtlpp::Function Func = GetCompiledFunction();
		check(Func);
        
		ns::Error Error;
		mtlpp::ComputePipelineDescriptor Descriptor;
		Descriptor.SetLabel(Func.GetName());
		Descriptor.SetComputeFunction(Func);
		if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
		{
			Descriptor.SetMaxTotalThreadsPerThreadgroup(NumThreadsX*NumThreadsY*NumThreadsZ);
		}
		
		if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesPipelineBufferMutability))
		{
			ns::AutoReleased<ns::Array<mtlpp::PipelineBufferDescriptor>> PipelineBuffers = Descriptor.GetBuffers();
			
			uint32 ImmutableBuffers = Bindings.ConstantBuffers | Bindings.ArgumentBuffers;
			while(ImmutableBuffers)
			{
				uint32 Index = __builtin_ctz(ImmutableBuffers);
				ImmutableBuffers &= ~(1 << Index);
				
				if (Index < ML_MaxBuffers)
				{
					ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = PipelineBuffers[Index];
					PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
				}
			}
			if (SideTableBinding > 0)
			{
				ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = PipelineBuffers[SideTableBinding];
				PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
			}
		}
		
		mtlpp::ComputePipelineState Kernel;
        mtlpp::ComputePipelineReflection Reflection;
		
		METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewComputePipeline: %d_%d"), SourceLen, SourceCRC)));
    #if METAL_DEBUG_OPTIONS
        if (GetMetalDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation METAL_STATISTICS_ONLY(|| GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
        {
			ns::AutoReleasedError ComputeError;
            mtlpp::AutoReleasedComputePipelineReflection ComputeReflection;
			
			NSUInteger ComputeOption = mtlpp::PipelineOption::ArgumentInfo|mtlpp::PipelineOption::BufferTypeInfo METAL_STATISTICS_ONLY(|NSUInteger(EMTLPipelineStats));
			Kernel = GetMetalDeviceContext().GetDevice().NewComputePipelineState(Descriptor, mtlpp::PipelineOption(ComputeOption), &ComputeReflection, &ComputeError);
			Error = ComputeError;
			Reflection = ComputeReflection;
        }
        else
    #endif
        {
			ns::AutoReleasedError ComputeError;
			Kernel = GetMetalDeviceContext().GetDevice().NewComputePipelineState(Descriptor, mtlpp::PipelineOption(0), nil, &ComputeError);
			Error = ComputeError;
        }
        
        if (Kernel == nil)
        {
            UE_LOG(LogRHI, Error, TEXT("*********** Error\n%s"), *FString(GetSourceCode()));
            UE_LOG(LogRHI, Fatal, TEXT("Failed to create compute kernel: %s"), *FString([Error description]));
        }
        
        Pipeline = [FMetalShaderPipeline new];
        Pipeline->ComputePipelineState = Kernel;
#if METAL_DEBUG_OPTIONS
        Pipeline->ComputePipelineReflection = Reflection;
        Pipeline->ComputeSource = GetSourceCode();
		if (Reflection)
		{
			Pipeline->ComputeDesc = Descriptor;
		}
#endif
        METAL_DEBUG_OPTION(FMemory::Memzero(Pipeline->ResourceMask, sizeof(Pipeline->ResourceMask)));
	}
	check(Pipeline);

	return Pipeline;
}

FMetalVertexShader::FMetalVertexShader(const TArray<uint8>& InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);
	
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	if (Header.Tessellation.Num())
	{
		auto const& Tess = Header.Tessellation[0];
		TessellationOutputAttribs = Tess.TessellationOutputAttribs;
		TessellationPatchCountBuffer = Tess.TessellationPatchCountBuffer;
		TessellationIndexBuffer = Tess.TessellationIndexBuffer;
		TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
		TessellationHSTFOutBuffer = Tess.TessellationHSTFOutBuffer;
		TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;
		TessellationControlPointIndexBuffer = Tess.TessellationControlPointIndexBuffer;
		TessellationOutputControlPoints = Tess.TessellationOutputControlPoints;
		TessellationDomain = Tess.TessellationDomain;
		TessellationInputControlPoints = Tess.TessellationInputControlPoints;
		TessellationMaxTessFactor = Tess.TessellationMaxTessFactor;
		TessellationPatchesPerThreadGroup = Tess.TessellationPatchesPerThreadGroup;
	}
#endif
}

FMetalVertexShader::FMetalVertexShader(const TArray<uint8>& InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
	
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	if (Header.Tessellation.Num())
	{
		auto const& Tess = Header.Tessellation[0];
		TessellationOutputAttribs = Tess.TessellationOutputAttribs;
		TessellationPatchCountBuffer = Tess.TessellationPatchCountBuffer;
		TessellationIndexBuffer = Tess.TessellationIndexBuffer;
		TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
		TessellationHSTFOutBuffer = Tess.TessellationHSTFOutBuffer;
		TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;
		TessellationControlPointIndexBuffer = Tess.TessellationControlPointIndexBuffer;
		TessellationOutputControlPoints = Tess.TessellationOutputControlPoints;
		TessellationDomain = Tess.TessellationDomain;
		TessellationInputControlPoints = Tess.TessellationInputControlPoints;
		TessellationMaxTessFactor = Tess.TessellationMaxTessFactor;
		TessellationPatchesPerThreadGroup = Tess.TessellationPatchesPerThreadGroup;
	}
#endif
}

mtlpp::Function FMetalVertexShader::GetFunction()
{
	return GetCompiledFunction();
}

FMetalPixelShader::FMetalPixelShader(const TArray<uint8>& InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);
}

FMetalPixelShader::FMetalPixelShader(const TArray<uint8>& InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

mtlpp::Function FMetalPixelShader::GetFunction()
{
	return GetCompiledFunction();
}

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
FMetalHullShader::FMetalHullShader(const TArray<uint8>& InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);
}

FMetalHullShader::FMetalHullShader(const TArray<uint8>& InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

mtlpp::Function FMetalHullShader::GetFunction()
{
	return GetCompiledFunction();
}

FMetalDomainShader::FMetalDomainShader(const TArray<uint8>& InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);
	
	// for VSHS
	auto const& Tess = Header.Tessellation[0];
	TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
	TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;
	
	switch (Tess.TessellationOutputWinding)
	{
		// NOTE: cw and ccw are flipped
		case EMetalOutputWindingMode::Clockwise:		TessellationOutputWinding = mtlpp::Winding::CounterClockwise; break;
		case EMetalOutputWindingMode::CounterClockwise:	TessellationOutputWinding = mtlpp::Winding::Clockwise; break;
		default: check(0);
	}
	
	switch (Tess.TessellationPartitioning)
	{
		case EMetalPartitionMode::Pow2:				TessellationPartitioning = mtlpp::TessellationPartitionMode::ModePow2; break;
		case EMetalPartitionMode::Integer:			TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeInteger; break;
		case EMetalPartitionMode::FractionalOdd:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalOdd; break;
		case EMetalPartitionMode::FractionalEven:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalEven; break;
		default: check(0);
	}
}

FMetalDomainShader::FMetalDomainShader(const TArray<uint8>& InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
	
	// for VSHS
	auto const& Tess = Header.Tessellation[0];
	TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
	TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;
	
	switch (Tess.TessellationOutputWinding)
	{
		// NOTE: cw and ccw are flipped
		case EMetalOutputWindingMode::Clockwise:		TessellationOutputWinding = mtlpp::Winding::CounterClockwise; break;
		case EMetalOutputWindingMode::CounterClockwise:	TessellationOutputWinding = mtlpp::Winding::Clockwise; break;
		default: check(0);
	}
	
	switch (Tess.TessellationPartitioning)
	{
		case EMetalPartitionMode::Pow2:				TessellationPartitioning = mtlpp::TessellationPartitionMode::ModePow2; break;
		case EMetalPartitionMode::Integer:			TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeInteger; break;
		case EMetalPartitionMode::FractionalOdd:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalOdd; break;
		case EMetalPartitionMode::FractionalEven:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalEven; break;
		default: check(0);
	}
}

mtlpp::Function FMetalDomainShader::GetFunction()
{
	return GetCompiledFunction();
}
#endif

FVertexShaderRHIRef FMetalDynamicRHI::RHICreateVertexShader(const TArray<uint8>& Code)
{
    @autoreleasepool {
	FMetalVertexShader* Shader = new FMetalVertexShader(Code);
	return Shader;
	}
}

FVertexShaderRHIRef FMetalDynamicRHI::RHICreateVertexShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	@autoreleasepool {

	checkSlow(Library && Library->IsNativeLibrary() && IsMetalPlatform(Library->GetPlatform()) && Library->GetPlatform() <= GMaxRHIShaderPlatform);
	
	FMetalShaderLibrary* MetalLibrary = ResourceCast(Library);
	return MetalLibrary->CreateVertexShader(Hash);
	
	}
}

FPixelShaderRHIRef FMetalDynamicRHI::RHICreatePixelShader(const TArray<uint8>& Code)
{
	@autoreleasepool {
	FMetalPixelShader* Shader = new FMetalPixelShader(Code);
	return Shader;
	}
}

FPixelShaderRHIRef FMetalDynamicRHI::RHICreatePixelShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	@autoreleasepool {
	
	checkSlow(Library && Library->IsNativeLibrary() && IsMetalPlatform(Library->GetPlatform()) && Library->GetPlatform() <= GMaxRHIShaderPlatform);
	
	FMetalShaderLibrary* MetalLibrary = ResourceCast(Library);
	return MetalLibrary->CreatePixelShader(Hash);
	
	}
}

FHullShaderRHIRef FMetalDynamicRHI::RHICreateHullShader(const TArray<uint8>& Code) 
{
	@autoreleasepool {
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	FMetalHullShader* Shader = new FMetalHullShader(Code);
#else
	FMetalHullShader* Shader = new FMetalHullShader;
	FMetalCodeHeader Header;
	Shader->Init(Code, Header);
#endif
	return Shader;
	}
}

FHullShaderRHIRef FMetalDynamicRHI::RHICreateHullShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	@autoreleasepool {
	
	checkSlow(Library && Library->IsNativeLibrary() && IsMetalPlatform(Library->GetPlatform()) && Library->GetPlatform() <= GMaxRHIShaderPlatform);
	
	FMetalShaderLibrary* MetalLibrary = ResourceCast(Library);
	return MetalLibrary->CreateHullShader(Hash);
	
	}
}

FDomainShaderRHIRef FMetalDynamicRHI::RHICreateDomainShader(const TArray<uint8>& Code) 
{
	@autoreleasepool {
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	FMetalDomainShader* Shader = new FMetalDomainShader(Code);
#else
	FMetalDomainShader* Shader = new FMetalDomainShader;
	FMetalCodeHeader Header;
	Shader->Init(Code, Header);
#endif
	return Shader;
	}
}

FDomainShaderRHIRef FMetalDynamicRHI::RHICreateDomainShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	@autoreleasepool {
	
	checkSlow(Library && Library->IsNativeLibrary() && IsMetalPlatform(Library->GetPlatform()) && Library->GetPlatform() <= GMaxRHIShaderPlatform);
	
	FMetalShaderLibrary* MetalLibrary = ResourceCast(Library);
	return MetalLibrary->CreateDomainShader(Hash);
	
	}
}

FGeometryShaderRHIRef FMetalDynamicRHI::RHICreateGeometryShader(const TArray<uint8>& Code) 
{
	@autoreleasepool {
	FMetalGeometryShader* Shader = new FMetalGeometryShader;
	FMetalCodeHeader Header;
	Shader->Init(Code, Header);
	return Shader;
	}
}

FGeometryShaderRHIRef FMetalDynamicRHI::RHICreateGeometryShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{
	@autoreleasepool {
	
	checkSlow(Library && Library->IsNativeLibrary() && IsMetalPlatform(Library->GetPlatform()) && Library->GetPlatform() <= GMaxRHIShaderPlatform);

	FMetalShaderLibrary* MetalLibrary = ResourceCast(Library);
	return MetalLibrary->CreateGeometryShader(Hash);
	
	}
}

FGeometryShaderRHIRef FMetalDynamicRHI::RHICreateGeometryShaderWithStreamOutput(const TArray<uint8>& Code, const FStreamOutElementList& ElementList,
	uint32 NumStrides, const uint32* Strides, int32 RasterizedStream)
{
	checkf(0, TEXT("Not supported yet"));
	return NULL;
}

FGeometryShaderRHIRef FMetalDynamicRHI::RHICreateGeometryShaderWithStreamOutput(const FStreamOutElementList& ElementList,
	uint32 NumStrides, const uint32* Strides, int32 RasterizedStream, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	@autoreleasepool {
	
	checkSlow(Library && Library->IsNativeLibrary() && IsMetalPlatform(Library->GetPlatform()) && Library->GetPlatform() <= GMaxRHIShaderPlatform);
	
	FMetalShaderLibrary* MetalLibrary = ResourceCast(Library);
	return MetalLibrary->CreateGeometryShaderWithStreamOutput(Hash, ElementList, NumStrides, Strides, RasterizedStream);
	
	}
}

FComputeShaderRHIRef FMetalDynamicRHI::RHICreateComputeShader(const TArray<uint8>& Code) 
{
	@autoreleasepool {
	return new FMetalComputeShader(Code);
	}
}

FComputeShaderRHIRef FMetalDynamicRHI::RHICreateComputeShader(FRHIShaderLibrary* Library, FSHAHash Hash)
{ 
	@autoreleasepool {
	
	checkSlow(Library && Library->IsNativeLibrary() && IsMetalPlatform(Library->GetPlatform()) && Library->GetPlatform() <= GMaxRHIShaderPlatform);

	FMetalShaderLibrary* MetalLibrary = ResourceCast(Library);
	return MetalLibrary->CreateComputeShader(Hash);
		
	}
}

FVertexShaderRHIRef FMetalDynamicRHI::CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code)
{
	return RHICreateVertexShader(Code);
}
FVertexShaderRHIRef FMetalDynamicRHI::CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return RHICreateVertexShader(Library, Hash);
}
FPixelShaderRHIRef FMetalDynamicRHI::CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code)
{
	return RHICreatePixelShader(Code);
}
FPixelShaderRHIRef FMetalDynamicRHI::CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return RHICreatePixelShader(Library, Hash);
}
FGeometryShaderRHIRef FMetalDynamicRHI::CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code)
{
	return RHICreateGeometryShader(Code);
}
FGeometryShaderRHIRef FMetalDynamicRHI::CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return RHICreateGeometryShader(Library, Hash);
}
FGeometryShaderRHIRef FMetalDynamicRHI::CreateGeometryShaderWithStreamOutput_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code, const FStreamOutElementList& ElementList, uint32 NumStrides, const uint32* Strides, int32 RasterizedStream)
{
	return RHICreateGeometryShaderWithStreamOutput(Code, ElementList, NumStrides, Strides, RasterizedStream);
}
FGeometryShaderRHIRef FMetalDynamicRHI::CreateGeometryShaderWithStreamOutput_RenderThread(class FRHICommandListImmediate& RHICmdList, const FStreamOutElementList& ElementList, uint32 NumStrides, const uint32* Strides, int32 RasterizedStream, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return RHICreateGeometryShaderWithStreamOutput(ElementList, NumStrides, Strides, RasterizedStream, Library, Hash);
}
FComputeShaderRHIRef FMetalDynamicRHI::CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code)
{
	return RHICreateComputeShader(Code);
}
FComputeShaderRHIRef FMetalDynamicRHI::CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return RHICreateComputeShader(Library, Hash);
}
FHullShaderRHIRef FMetalDynamicRHI::CreateHullShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code)
{
	return RHICreateHullShader(Code);
}
FHullShaderRHIRef FMetalDynamicRHI::CreateHullShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return RHICreateHullShader(Library, Hash);
}
FDomainShaderRHIRef FMetalDynamicRHI::CreateDomainShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code)
{
	return RHICreateDomainShader(Code);
}
FDomainShaderRHIRef FMetalDynamicRHI::CreateDomainShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash)
{
	return RHICreateDomainShader(Library, Hash);
}

static FCriticalSection LoadedShaderLibraryMutex;
static TMap<FString, FRHIShaderLibrary*> LoadedShaderLibraryMap;

FMetalShaderLibrary::FMetalShaderLibrary(EShaderPlatform InPlatform, FString const& Name, TArray<mtlpp::Library> InLibrary, FMetalShaderMap const& InMap, const FString& InShaderLibraryFilename)
: FRHIShaderLibrary(InPlatform, Name)
, Library(InLibrary)
, Map(InMap)
, ShaderLibraryFilename(InShaderLibraryFilename)
{
#if !UE_BUILD_SHIPPING
	DebugFile = nullptr;

	FName PlatformName = LegacyShaderPlatformToShaderFormat(InPlatform);
	FString LibName = FString::Printf(TEXT("%s_%s"), *Name, *PlatformName.GetPlainNameString());
	LibName.ToLowerInline();
	FString Path = FPaths::ProjectContentDir() / LibName + TEXT(".zip");
	
	if ( IFileManager::Get().FileExists(*Path) )
	{
		DebugFile = FMetalShaderDebugCache::Get().GetDebugFile(Path);
	}
#endif
}

FMetalShaderLibrary::~FMetalShaderLibrary()
{
	FScopeLock Lock(&LoadedShaderLibraryMutex);
	LoadedShaderLibraryMap.Remove(ShaderLibraryFilename);
}

bool FMetalShaderLibrary::ContainsEntry(const FSHAHash& Hash)
{
	FMetalShadeEntry* Code = Map.HashMap.Find(Hash);
	return (Code != nullptr);
}

bool FMetalShaderLibrary::RequestEntry(const FSHAHash& Hash, FArchive* Ar)
{
	FMetalShadeEntry* Code = Map.HashMap.Find(Hash);
	return (Code != nullptr);
}

FPixelShaderRHIRef FMetalShaderLibrary::CreatePixelShader(const FSHAHash& Hash)
{
	FMetalShadeEntry* Code = Map.HashMap.Find(Hash);
	if (Code)
	{
		FMetalPixelShader* Shader = new FMetalPixelShader(Code->Data, Library[Code->Index]);
		if (Shader->GetFunction())
		{
			return Shader;
		}
		else
		{
			delete Shader;
		}
	}
	
	UE_LOG(LogMetal, Error, TEXT("Failed to find Pixel Shader with SHA: %s"), *Hash.ToString());
	return FPixelShaderRHIRef();
}

FVertexShaderRHIRef FMetalShaderLibrary::CreateVertexShader(const FSHAHash& Hash)
{
	FMetalShadeEntry* Code = Map.HashMap.Find(Hash);
	if (Code)
	{
		FMetalVertexShader* Shader = new FMetalVertexShader(Code->Data, Library[Code->Index]);
		if (Shader->GetFunction())
		{
			return Shader;
		}
		else
		{
			delete Shader;
		}
	}
	UE_LOG(LogMetal, Error, TEXT("Failed to find Vertex Shader with SHA: %s"), *Hash.ToString());
	return FVertexShaderRHIRef();
}

FHullShaderRHIRef FMetalShaderLibrary::CreateHullShader(const FSHAHash& Hash)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	FMetalShadeEntry* Code = Map.HashMap.Find(Hash);
	if (Code)
	{
		FMetalHullShader* Shader = new FMetalHullShader(Code->Data, Library[Code->Index]);
		if(Shader->GetFunction())
		{
			return Shader;
		}
		else
		{
			delete Shader;
		}
	}
	UE_LOG(LogMetal, Error, TEXT("Failed to find Hull Shader with SHA: %s"), *Hash.ToString());
	return FHullShaderRHIRef();
#else
	checkf(0, TEXT("Not supported"));
	return NULL;
#endif
}

FDomainShaderRHIRef FMetalShaderLibrary::CreateDomainShader(const FSHAHash& Hash)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	FMetalShadeEntry* Code = Map.HashMap.Find(Hash);
	if (Code)
	{
		FMetalDomainShader* Shader = new FMetalDomainShader(Code->Data, Library[Code->Index]);
		if (Shader->GetFunction())
		{
			return Shader;
		}
		else
		{
			delete Shader;
		}
	}
	UE_LOG(LogMetal, Error, TEXT("Failed to find Domain Shader with SHA: %s"), *Hash.ToString());
	return FDomainShaderRHIRef();
#else
	checkf(0, TEXT("Not supported"));
	return NULL;
#endif
}

FGeometryShaderRHIRef FMetalShaderLibrary::CreateGeometryShader(const FSHAHash& Hash)
{
	checkf(0, TEXT("Not supported"));
	return NULL;
}

FGeometryShaderRHIRef FMetalShaderLibrary::CreateGeometryShaderWithStreamOutput(const FSHAHash& Hash, const FStreamOutElementList& ElementList, uint32 NumStrides, const uint32* Strides, int32 RasterizedStream)
{
	checkf(0, TEXT("Not supported yet"));
	return NULL;
}

FComputeShaderRHIRef FMetalShaderLibrary::CreateComputeShader(const FSHAHash& Hash)
{
	FMetalShadeEntry* Code = Map.HashMap.Find(Hash);
	if (Code)
	{
		FMetalComputeShader* Shader = new FMetalComputeShader(Code->Data, Library[Code->Index]);
		if (Shader->GetPipeline())
		{
			return Shader;
		}
		else
		{
			delete Shader;
		}
	}
	UE_LOG(LogMetal, Error, TEXT("Failed to find Compute Shader with SHA: %s"), *Hash.ToString());
	return FComputeShaderRHIRef();
}

//
//Library Iterator
//
FRHIShaderLibrary::FShaderLibraryEntry FMetalShaderLibrary::FMetalShaderLibraryIterator::operator*() const
{
	FShaderLibraryEntry Entry;
	
	Entry.Hash = IteratorImpl->Key;
	Entry.Frequency = (EShaderFrequency)IteratorImpl->Value.Type;
	Entry.Platform = GetLibrary()->GetPlatform();
	
	return Entry;
}

FRHIShaderLibraryRef FMetalDynamicRHI::RHICreateShaderLibrary_RenderThread(class FRHICommandListImmediate& RHICmdList, EShaderPlatform Platform, FString FilePath, FString Name)
{
	return RHICreateShaderLibrary(Platform, FilePath, Name);
}

FRHIShaderLibraryRef FMetalDynamicRHI::RHICreateShaderLibrary(EShaderPlatform Platform, FString const& FilePath, FString const& Name)
{
	@autoreleasepool {
	FRHIShaderLibraryRef Result = nullptr;
	
	FName PlatformName = LegacyShaderPlatformToShaderFormat(Platform);
	FString LibName = FString::Printf(TEXT("%s_%s"), *Name, *PlatformName.GetPlainNameString());
	LibName.ToLowerInline();
	
	FMetalShaderMap Map;
	FString BinaryShaderFile = FilePath / LibName + METAL_MAP_EXTENSION;

	if ( IFileManager::Get().FileExists(*BinaryShaderFile) == false )
	{
		// the metal map files are stored in UFS file system
		// for pak files this means they might be stored in a different location as the pak files will mount them to the project content directory
		// the metal libraries are stores non UFS and could be anywhere on the file system.
		// if we don't find the metalmap file straight away try the pak file path
		BinaryShaderFile = FPaths::ProjectContentDir() / LibName + METAL_MAP_EXTENSION;
	}

	FScopeLock Lock(&LoadedShaderLibraryMutex);
	FRHIShaderLibrary** FoundShaderLibrary = LoadedShaderLibraryMap.Find(BinaryShaderFile);
	if (FoundShaderLibrary)
	{
		return *FoundShaderLibrary;
	}

	FArchive* BinaryShaderAr = IFileManager::Get().CreateFileReader(*BinaryShaderFile);

	if( BinaryShaderAr != NULL )
	{
		*BinaryShaderAr << Map;
		BinaryShaderAr->Flush();
		delete BinaryShaderAr;
		
		// Would be good to check the language version of the library with the archive format here.
		if (Map.Format == PlatformName.GetPlainNameString())
		{
			TArray<mtlpp::Library> Libraries;
			for (uint32 i = 0; i < Map.Count; i++)
			{
				FString MetalLibraryFilePath = (FilePath / LibName) + FString::Printf(TEXT(".%d"), i) + METAL_LIB_EXTENSION;
				MetalLibraryFilePath = FPaths::ConvertRelativePathToFull(MetalLibraryFilePath);
	#if !PLATFORM_MAC
				MetalLibraryFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*MetalLibraryFilePath);
	#endif
				
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewLibraryFile: %s"), *MetalLibraryFilePath)));
				NSError* Error;
				mtlpp::Library Library = [GetMetalDeviceContext().GetDevice() newLibraryWithFile:MetalLibraryFilePath.GetNSString() error:&Error];
				if (Library != nil)
				{
					Libraries.Add(Library);
				}
				else
				{
					UE_LOG(LogMetal, Display, TEXT("Failed to create library: %s"), *FString(Error.description));
					return nullptr;
				}
			}

			Result = new FMetalShaderLibrary(Platform, Name, Libraries, Map, BinaryShaderFile);
			LoadedShaderLibraryMap.Add(BinaryShaderFile, Result.GetReference());
		}
		else
		{
			UE_LOG(LogMetal, Display, TEXT("Wrong shader platform wanted: %s, got: %s"), *LibName, *Map.Format);
		}
	}
	else
	{
		UE_LOG(LogMetal, Display, TEXT("No .metalmap file found for %s!"), *LibName);
	}
	
	return Result;
	}
}

FBoundShaderStateRHIRef FMetalDynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIHullShader* HullShaderRHI,
	FRHIDomainShader* DomainShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI
	)
{
	NOT_SUPPORTED("RHICreateBoundShaderState");
	return nullptr;
}

FMetalShaderParameterCache::FMetalShaderParameterCache()
{
	for (int32 ArrayIndex = 0; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		PackedGlobalUniforms[ArrayIndex] = nullptr;
		PackedGlobalUniformsSizes[ArrayIndex] = 0;
		PackedGlobalUniformDirty[ArrayIndex].LowVector = 0;
		PackedGlobalUniformDirty[ArrayIndex].HighVector = 0;
	}
}

void FMetalShaderParameterCache::ResizeGlobalUniforms(uint32 TypeIndex, uint32 UniformArraySize)
{
	if (!PackedGlobalUniforms[TypeIndex])
	{
		PackedGlobalUniforms[TypeIndex] = [[FMetalBufferData alloc] initWithSize:UniformArraySize];
	}
	else
	{
		PackedGlobalUniforms[TypeIndex]->Data = (uint8*)FMemory::Realloc(PackedGlobalUniforms[TypeIndex]->Data, UniformArraySize);
		PackedGlobalUniforms[TypeIndex]->Len = UniformArraySize;
	}
	PackedGlobalUniformsSizes[TypeIndex] = UniformArraySize;
	PackedGlobalUniformDirty[TypeIndex].LowVector = 0;
	PackedGlobalUniformDirty[TypeIndex].HighVector = 0;
}

/** Destructor. */
FMetalShaderParameterCache::~FMetalShaderParameterCache()
{
	for (int32 ArrayIndex = 0; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		[PackedGlobalUniforms[ArrayIndex] release];
	}
}

/**
 * Invalidates all existing data.
 */
void FMetalShaderParameterCache::Reset()
{
	for (int32 ArrayIndex = 0; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		PackedGlobalUniformDirty[ArrayIndex].LowVector = 0;
		PackedGlobalUniformDirty[ArrayIndex].HighVector = 0;
	}
}

static const int SizeOfFloat = sizeof(float);

/**
 * Marks all uniform arrays as dirty.
 */
void FMetalShaderParameterCache::MarkAllDirty()
{
	for (int32 ArrayIndex = 0; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		PackedGlobalUniformDirty[ArrayIndex].LowVector = 0;
		PackedGlobalUniformDirty[ArrayIndex].HighVector = PackedGlobalUniformsSizes[ArrayIndex] / SizeOfFloat;
	}
}

/**
 * Set parameter values.
 */
void FMetalShaderParameterCache::Set(uint32 BufferIndexName, uint32 ByteOffset, uint32 NumBytes, const void* NewValues)
{
	uint32 BufferIndex = CrossCompiler::PackedTypeNameToTypeIndex(BufferIndexName);
	check(BufferIndex < CrossCompiler::PACKED_TYPEINDEX_MAX);
	check(PackedGlobalUniforms[BufferIndex]);
	check(ByteOffset + NumBytes <= PackedGlobalUniformsSizes[BufferIndex]);
	PackedGlobalUniformDirty[BufferIndex].LowVector = FMath::Min(PackedGlobalUniformDirty[BufferIndex].LowVector, ByteOffset / SizeOfFloat);
	PackedGlobalUniformDirty[BufferIndex].HighVector = FMath::Max(PackedGlobalUniformDirty[BufferIndex].HighVector, (ByteOffset + NumBytes + SizeOfFloat - 1) / SizeOfFloat);
	FMemory::Memcpy(PackedGlobalUniforms[BufferIndex]->Data + ByteOffset, NewValues, NumBytes);
}

void FMetalShaderParameterCache::CommitPackedGlobals(FMetalStateCache* Cache, FMetalCommandEncoder* Encoder, uint32 Frequency, const FMetalShaderBindings& Bindings)
{
	// copy the current uniform buffer into the ring buffer to submit
	for (int32 Index = 0; Index < Bindings.PackedGlobalArrays.Num(); ++Index)
	{
		int32 UniformBufferIndex = Bindings.PackedGlobalArrays[Index].TypeIndex;
 
		// is there any data that needs to be copied?
		if (PackedGlobalUniformDirty[Index].HighVector > 0)
		{
			uint32 TotalSize = Bindings.PackedGlobalArrays[Index].Size;
			uint32 SizeToUpload = PackedGlobalUniformDirty[Index].HighVector * SizeOfFloat;
			
			//@todo-rco: Temp workaround
			SizeToUpload = TotalSize;
			
			//@todo-rco: Temp workaround
			uint32 Size = FMath::Min(TotalSize, SizeToUpload);
			if (Size > MetalBufferPageSize)
			{
				uint8 const* Bytes = PackedGlobalUniforms[Index]->Data;
				ns::AutoReleased<FMetalBuffer> Buffer(Encoder->GetRingBuffer().NewBuffer(Size, 0));
				FMemory::Memcpy((uint8*)Buffer.GetContents(), Bytes, Size);
				Cache->SetShaderBuffer((EMetalShaderStages)Frequency, Buffer, nil, 0, Size, UniformBufferIndex, mtlpp::ResourceUsage::Read);
			}
			else
			{
				PackedGlobalUniforms[Index]->Len = Size;
				Cache->SetShaderBuffer((EMetalShaderStages)Frequency, nil, nil, 0, 0, UniformBufferIndex, mtlpp::ResourceUsage(0));
				Cache->SetShaderBuffer((EMetalShaderStages)Frequency, nil, PackedGlobalUniforms[Index], 0, Size, UniformBufferIndex, mtlpp::ResourceUsage::Read);
			}

			// mark as clean
			PackedGlobalUniformDirty[Index].HighVector = 0;
		}
	}
}
