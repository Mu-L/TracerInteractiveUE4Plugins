/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef APEX_MIRRORED_ARRAY_H
#define APEX_MIRRORED_ARRAY_H

#include "ApexDefs.h"

#include "ApexMirrored.h"
#include <new>

#if defined(__CUDACC__) || PX_ANDROID || PX_PS4 || PX_LINUX_FAMILY || PX_OSX
#define DEFAULT_NAME "unassigned"
#else
#include <typeinfo>
#define DEFAULT_NAME typeid(T).name()
#endif

#pragma warning(push)
#pragma warning(disable:4348)


namespace nvidia
{
namespace apex
{

template <class T>
class ApexMirroredArray
{
	PX_NOCOPY(ApexMirroredArray);

public:
	/*!
	Default array constructor.
	Initialize an empty array
	*/
	explicit PX_INLINE ApexMirroredArray(SceneIntl& scene, PX_ALLOC_INFO_PARAMS_DECL("", 0, DEFAULT_NAME, UNASSIGNED)) :
		mData(scene, PX_ALLOC_INFO_PARAMS_INPUT()), mCapacity(0), mSize(0) {};

	/*!
	Default destructor
	*/
	PX_INLINE ~ApexMirroredArray()
	{
		mData.free();
	}

	/*!
	Return an element from this array. Operation is O(1).
	\param i
	The index of the element that will be returned.
	\return
	Element i in the array.
	*/
	PX_INLINE const T& get(uint32_t i) const
	{
		return mData.getCpuPtr()[i];
	}

	/*!
	Return an element from this array. Operation is O(1).
	\param i
	The index of the element that will be returned.
	\return
	Element i in the array.
	*/
	PX_INLINE T& get(uint32_t i)
	{
		return mData.getCpuPtr()[i];
	}

	/*!
	Array indexing operator.
	\param i
	The index of the element that will be returned.
	\return
	The element i in the array.
	*/
	PX_INLINE const T& operator[](uint32_t i) const
	{
		return get(i);
	}

	/*!
	Array indexing operator.
	\param i
	The index of the element that will be returned.
	\return
	The element i in the array.
	*/
	PX_INLINE T& operator[](uint32_t i)
	{
		return get(i);
	}

	/*!
	\return
	returns whether GPU buffer has been allocated for this array
	*/
	PX_INLINE bool cpuPtrIsValid() const
	{
		return mData.cpuPtrIsValid();
	}

	/*!
	Returns the plain array representation.
	\return
	The sets representation.
	*/
	PX_INLINE T* getPtr() const
	{
		return mData.getCpuPtr();
	}

#if APEX_CUDA_SUPPORT
	/*!
	\return
	returns whether GPU buffer has been allocated for this array
	*/
	PX_INLINE bool gpuPtrIsValid() const
	{
		return mData.gpuPtrIsValid();
	}

	PX_INLINE T* getGpuPtr() const
	{
		return mData.getGpuPtr();
	}

	PX_INLINE void copyDeviceToHostDesc(PxGpuCopyDesc& desc, uint32_t size, uint32_t offset) const
	{
		PX_ASSERT(gpuPtrIsValid() && cpuPtrIsValid());
		if (size == 0)
		{
			size = mSize;
		}
		mData.copyDeviceToHostDesc(desc, sizeof(T) * size, sizeof(T) * offset);
	}
	PX_INLINE void copyDeviceToHostQ(PxGpuCopyDescQueue& queue, uint32_t size = 0, uint32_t offset = 0) const
	{
		PxGpuCopyDesc desc;
		copyDeviceToHostDesc(desc, size, offset);
		queue.enqueue(desc);
	}

	PX_INLINE void copyHostToDeviceDesc(PxGpuCopyDesc& desc, uint32_t size, uint32_t offset) const
	{
		PX_ASSERT(gpuPtrIsValid() && cpuPtrIsValid());
		if (size == 0)
		{
			size = mSize;
		}
		mData.copyHostToDeviceDesc(desc, sizeof(T) * size, sizeof(T) * offset);
	}
	PX_INLINE void copyHostToDeviceQ(PxGpuCopyDescQueue& queue, uint32_t size = 0, uint32_t offset = 0) const
	{
		PxGpuCopyDesc desc;
		copyHostToDeviceDesc(desc, size, offset);
		queue.enqueue(desc);
	}
	PX_INLINE void swapGpuPtr(ApexMirroredArray<T>& other)
	{
		PX_ASSERT(mCapacity == other.mCapacity);

		mData.swapGpuPtr(other.mData);
	}
#endif /* APEX_CUDA_SUPPORT */

	/*!
	Returns the number of entries in the array. This can, and probably will,
	differ from the array size.
	\return
	The number of of entries in the array.
	*/
	PX_INLINE uint32_t getSize() const
	{
		return mSize;
	}

	PX_INLINE size_t getByteSize() const
	{
		return mData.getByteSize();
	}

	PX_INLINE char* getName() const
	{
		return mData.getName();
	}

	/*!
	Clears the array.
	*/
	PX_INLINE void clear()
	{
		mSize = 0;
		mData.free();
		mCapacity = 0;
	}

	//////////////////////////////////////////////////////////////////////////
	/*!
	Resize array
	*/
	//////////////////////////////////////////////////////////////////////////
	PX_INLINE void setSize(const uint32_t size, ApexMirroredPlace::Enum place = ApexMirroredPlace::DEFAULT)
	{
		if (size > mCapacity)
		{
			mCapacity = size;
		}
		mData.realloc(sizeof(T) * mCapacity, place);
		mSize = size;
	}

	//////////////////////////////////////////////////////////////////////////
	/*!
	Ensure that the array has at least size capacity.
	*/
	//////////////////////////////////////////////////////////////////////////
	PX_INLINE void reserve(const uint32_t capacity, ApexMirroredPlace::Enum place = ApexMirroredPlace::DEFAULT)
	{
		if (capacity > mCapacity)
		{
			mCapacity = capacity;
		}
		mData.realloc(sizeof(T) * mCapacity, place);
	}

	//////////////////////////////////////////////////////////////////////////
	/*!
	Query the capacity(allocated mem) for the array.
	*/
	//////////////////////////////////////////////////////////////////////////
	PX_INLINE uint32_t getCapacity()
	{
		return mCapacity;
	}

private:
	ApexMirrored<T> mData;
	uint32_t    mCapacity;
	uint32_t    mSize;
};

}
} // end namespace nvidia::apex

#pragma warning(pop)

#endif
