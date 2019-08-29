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


#ifndef PX_PHYSICS_COMMON_TASK
#define PX_PHYSICS_COMMON_TASK

#include "PxTask.h"
#include "PxTaskManager.h"
#include "CmPhysXCommon.h"
#include "PsUserAllocated.h"
#include "PsAtomic.h"
#include "PsMutex.h"
#include "PsSync.h"
#include "PxCpuDispatcher.h"
#include "PsFPU.h"
#include "PsInlineArray.h"

namespace nvidia
{
namespace Cm
{
	// wrapper around the public PxLightCpuTask
	// internal SDK tasks should be inherited from
	// this and override the runInternal() method
	// to ensure that the correct floating point 
	// state is set / reset during execution
	class Task : public PxLightCpuTask
	{
	public:

		virtual void run()
		{
			physx::PX_SIMD_GUARD;
			runInternal();
		}

		virtual void runInternal()=0;
	};

	// same as Cm::Task but inheriting from PxBaseTask
	// instead of PxLightCpuTask
	class BaseTask : public PxBaseTask
	{
	public:

		virtual void run()
		{
			physx::PX_SIMD_GUARD;
			runInternal();
		}

		virtual void runInternal()=0;
	};

	template <class T, void (T::*Fn)(PxBaseTask*) >
	class DelegateTask : public Cm::Task, public shdfnd::UserAllocated
	{
	public:

		DelegateTask(T* obj, const char* name) : 
		  mObj(obj), mName(name) { }

		virtual void runInternal()
		{
			(mObj->*Fn)((PxBaseTask*)mCont);
		}

		virtual const char* getName() const
		{
			return mName;
		}

		void setObject(T* obj) { mObj = obj; }

	private:
		T* mObj;
		const char* mName;
	};


	/**
	\brief A task that maintains a list of dependent tasks.
	
	This task maintains a list of dependent tasks that have their reference counts 
	reduced on completion of the task.

	The refcount is incremented every time a dependent task is added.
	*/
	class FanoutTask : public Cm::BaseTask
	{
		PX_NOCOPY(FanoutTask)
	public:
		FanoutTask(const char* name) : Cm::BaseTask(), mRefCount(0), mName(name), mNotifySubmission(false) {}

		virtual void runInternal() {}

		virtual const char* getName() const { return mName; }

		/**
		Swap mDependents with mReferencesToRemove when refcount goes to 0.
		*/
		virtual void removeReference()
		{
			nvidia::Mutex::ScopedLock lock(mMutex);
			if (!nvidia::atomicDecrement(&mRefCount))
			{
				// prevents access to mReferencesToRemove until release
				nvidia::atomicIncrement(&mRefCount);
				mNotifySubmission = false;
				PX_ASSERT(mReferencesToRemove.empty());
				for (uint32_t i = 0; i < mDependents.size(); i++)
					mReferencesToRemove.pushBack(mDependents[i]);
				mDependents.clear();
				mTm->getCpuDispatcher()->submitTask(*this);
			}
		}

		/** 
		\brief Increases reference count
		*/
		virtual void addReference()
		{
			nvidia::Mutex::ScopedLock lock(mMutex);
			nvidia::atomicIncrement(&mRefCount);
			mNotifySubmission = true;
		}

		/** 
		\brief Return the ref-count for this task 
		*/
		PX_INLINE int32_t getReference() const
		{
			return mRefCount;
		}

		/**
		Sets the task manager. Doesn't increase the reference count.
		*/
		PX_INLINE void setTaskManager(PxTaskManager& tm)
		{
			mTm = &tm;
		}

		/**
		Adds a dependent task. It also sets the task manager querying it from the dependent task.  
		The refcount is incremented every time a dependent task is added.
		*/
		PX_INLINE void addDependent(PxBaseTask& dependent)
		{
			nvidia::Mutex::ScopedLock lock(mMutex);
			nvidia::atomicIncrement(&mRefCount);
			mTm = dependent.getTaskManager();
			mDependents.pushBack(&dependent);
			dependent.addReference();
			mNotifySubmission = true;
		}

		/**
		Reduces reference counts of the continuation task and the dependent tasks, also 
		clearing the copy of continuation and dependents task list.
		*/
		virtual void release()
		{
			nvidia::Mutex::ScopedLock lock(mMutex);
			for (uint32_t i = 0, n = mReferencesToRemove.size(); i < n; ++i)
				mReferencesToRemove[i]->removeReference();
			mReferencesToRemove.clear();
			// allow access to mReferencesToRemove again
			if (mNotifySubmission)
			{
				removeReference();
			}
			else
			{
				nvidia::atomicDecrement(&mRefCount);
			}
		}

	protected:
		volatile int32_t mRefCount;
		const char* mName;
		nvidia::InlineArray<PxBaseTask*, 4> mDependents;
		nvidia::InlineArray<PxBaseTask*, 4> mReferencesToRemove;
		bool mNotifySubmission;
		nvidia::Mutex mMutex; // guarding mDependents and mNotifySubmission
	};


	/**
	\brief Specialization of FanoutTask class in order to provide the delegation mechanism.
	*/
	template <class T, void (T::*Fn)(PxBaseTask*) >
	class DelegateFanoutTask : public FanoutTask, public shdfnd::UserAllocated
	{
	public:

		DelegateFanoutTask(T* obj, const char* name) : 
		  FanoutTask(name), mObj(obj) { }

		  virtual void runInternal()
		  {
			  PxBaseTask* continuation = mDependents.empty() ? NULL : mDependents[0];
			  (mObj->*Fn)(continuation);
		  }

		  void setObject(T* obj) { mObj = obj; }

	private:
		T* mObj;
	};

} // namespace Cm

}

#endif
