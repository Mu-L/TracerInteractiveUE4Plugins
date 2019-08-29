/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  
#include <assert.h>
#include "MeshImport.h"
#include "ImportEzm.h"


#ifdef WIN32
#ifdef MESHIMPORTEZM_EXPORTS
#define MESHIMPORTEZM_API __declspec(dllexport)
#else
#define MESHIMPORTEZM_API __declspec(dllimport)
#endif
#else
#define MESHIMPORTEZM_API
#endif

#ifndef _MSC_VER

// Other platforms do not have these nice functions
// TODO: we should probably share this with other MeshImporters

inline void *_aligned_malloc(size_t size, size_t /*alignment*/)
{
	void *memblock = malloc(size);
	if( reinterpret_cast<size_t>(memblock) & (16 - 1) ) {
		assert( 0 && "malloc result is not 16-byte aligned" );
		free(memblock);
		return 0;
	}

	return memblock;
}

inline void _aligned_free(void *memblock)
{
	free(memblock);
}

#endif

#pragma warning(disable:4100)

bool doShutdown(void);

extern "C"
{
	MESHIMPORTEZM_API mimp::MeshImporter * getInterface(mimp::MiI32 version_number);
};

namespace mimp
{

class MyMeshImportEzm : public mimp::MeshImporter
{
public:
	MyMeshImportEzm(void)
	{
	}

	virtual ~MyMeshImportEzm(void)
	{
	}

	bool shutdown(void)
	{
		return doShutdown();
	}

	virtual MiI32              getExtensionCount(void)
	{
		return 2;
	}
	virtual const char* getExtension(MiI32 index)
	{
		switch (index)
		{
		case 0:
			return ".ezm";
		case 1:
			return ".ezb";
		default:
			return NULL;
		}
	}

	virtual const char * getDescription(MiI32 index)
	{
		switch (index)
		{
		case 0:
			return "EZ-Mesh format";
		case 1:
			return "EZ-Mesh binary format";
		default:
			return NULL;
		}
	}


	virtual bool importMesh(const char *meshName,const void *data,MiU32 dlen,mimp::MeshImportInterface *callback,const char *options,MeshImportApplicationResource *appResource)
	{
		bool ret = false;

		MeshImporter *mi = createMeshImportEZM();
		if ( mi )
		{
			ret = mi->importMesh(meshName,data,dlen,callback,options,appResource);
			releaseMeshImportEZM(mi);
		}

		return ret;
	}

	virtual const void * saveMeshSystem(MeshSystem * /*ms*/,MiU32 & /*dlen*/,bool /*binary*/) 
	{
		return NULL;
	}

	virtual void releaseSavedMeshSystem(const void * /*mem*/) 
	{

	}
};

} // namespace mimp






static mimp::MyMeshImportEzm *gInterface=0;

extern "C"
{
#ifdef PLUGINS_EMBEDDED
	mimp::MeshImporter * getInterfaceMeshImportEzm(mimp::MiI32 version_number)
#else
	MESHIMPORTEZM_API mimp::MeshImporter * getInterface(mimp::MiI32 version_number)
#endif
	{
		if ( gInterface == 0 && version_number == MESHIMPORT_VERSION )
		{
			gInterface = new mimp::MyMeshImportEzm;
		}
		return static_cast<mimp::MeshImporter *>(gInterface);
	};

};  // End of namespace PATHPLANNING

#ifndef PLUGINS_EMBEDDED



bool doShutdown(void)
{
	bool ret = false;
	if ( gInterface )
	{
		ret = true;
		delete gInterface;
		gInterface = 0;
	}
	return ret;
}



#ifdef WIN32

#include <windows.h>

BOOL APIENTRY DllMain( HANDLE ,
					  DWORD  ul_reason_for_call,
					  LPVOID )
{
	mimp::MiI32 ret = 0;

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		ret = 1;
		break;
	case DLL_THREAD_ATTACH:
		ret = 2;
		break;
	case DLL_THREAD_DETACH:
		ret = 3;
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

#endif

#endif
