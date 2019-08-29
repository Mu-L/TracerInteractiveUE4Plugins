/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#include "RTdef.h"
#if RT_COMPILE
#ifndef SIM_SCENE_BASE
#define SIM_SCENE_BASE

#include "PxSimulationEventCallback.h"
#include <PsArray.h>
#include <PsUserAllocated.h>
#include <PsHashMap.h>

#include "RTdef.h"

namespace nvidia
{
namespace fracture
{
namespace base
{

class Actor;
class Compound;
class Convex;
class FracturePattern;
class CompoundCreator;
class Delaunay2d;
class Delaunay3d;
class PolygonTriangulator;
class IslandDetector;
class MeshClipper;

// -----------------------------------------------------------------------------------
class SimScene :  
	public PxSimulationEventCallback, public UserAllocated
{
	friend class Actor;
public:
	static SimScene* createSimScene(PxPhysics *pxPhysics, PxCooking *pxCooking, PxScene *scene, float minConvexSize, PxMaterial* defaultMat, const char *resourcePath);
protected:
	SimScene(PxPhysics *pxPhysics, PxCooking *pxCooking, PxScene *scene, float minConvexSize, PxMaterial* defaultMat, const char *resourcePath);
public:
	// Allow the Destructible module to release things in a proper order
	void restoreUserCallbacks();
	virtual ~SimScene();

	// Creates Scene Level Singletons
	virtual void createSingletons();
	// Access singletons
	CompoundCreator* getCompoundCreator() {return mCompoundCreator;}
	Delaunay2d* getDelaunay2d() {return mDelaunay2d;}
	Delaunay3d* getDelaunay3d() {return mDelaunay3d;}
	PolygonTriangulator* getPolygonTriangulator() {return mPolygonTriangulator;}
	IslandDetector* getIslandDetector() {return mIslandDetector;}
	MeshClipper* getMeshClipper() {return mMeshClipper;}

	// Create non-Singletons
	virtual Actor* createActor();
	virtual Convex* createConvex();
	virtual Compound* createCompound(const FracturePattern *pattern, const FracturePattern *secondaryPattern = NULL, float contactOffset = 0.005f, float restOffset = -0.001f);
	virtual FracturePattern* createFracturePattern();
	virtual void clear();
	void addCompound(Compound *m);
	void removeCompound(Compound *m);
	// perform deferred deletion
	void deleteCompounds();
	// 
	bool findCompound(const Compound* c, int& actorNr, int& compoundNr);

	void removeActor(Actor* a);

	// Profiler hooks
	virtual void profileBegin(const char* /*name*/) {}
	virtual void profileEnd(const char* /*name*/) {}

	bool rayCast(const PxVec3 &orig, const PxVec3 &dir, float &dist, int &actorNr, int &compoundNr, int &convexNr, PxVec3 &normal) const;

	bool patternFracture(const PxVec3 &orig, const PxVec3 &dir, 
		const PxMat33 patternTransform,  float impactRadius = 0.0f, float radialImpulse = 0.0f, float directionalImpulse = 0.0f);

	virtual void playSound(const char * /*name*/, int /*nr*/ = -1) {}

	// accessors
	nvidia::Array<Compound*> getCompounds(); //{ return mCompounds; }
	nvidia::Array<Actor*> getActors() { return mActors; }
	PxPhysics* getPxPhysics() { return mPxPhysics; }
	PxCooking* getPxCooking() { return mPxCooking; }
	PxScene* getScene() { return mScene; }

	//ConvexRenderer &getConvexRenderer() { return mConvexRenderer; }

	void preSim(float dt);
	void postSim(float dt); //, RegularCell3D* fluidSim);

	void setPlaySounds(bool play) { mPlaySounds = play; }
	void setContactImpactRadius(float radius) { mContactImpactRadius = radius; }
	void setNumNoFractureFrames(int num) { mNumNoFractureFrames = num; }

	void setCamera(const PxVec3 &pos, const PxVec3 &dir, const PxVec3 &up, float fov ) {
		mCameraPos = pos; mCameraDir = dir; mCameraUp = up; mCameraFov = fov;
	}

	//void draw(bool useShader, Shader* particleShader = NULL) {}
	//void setShaderMaterial(Shader* shader, const ShaderMaterial& mat) {this->mShader = shader; this->mShaderMat = mat;}
	//void setFractureForceThreshold(float threshold) { mFractureForceThreshold = threshold; }

	PxMaterial *getPxDefaultMaterial() { return mPxDefaultMaterial; }

	void toggleDebugDrawing() { mDebugDraw = !mDebugDraw; }

	virtual bool pickStart(const PxVec3 &orig, const PxVec3 &dir);
	virtual void pickMove(const PxVec3 &orig, const PxVec3 &dir);
	virtual void pickRelease();
	PxRigidDynamic* getPickActor() { return mPickActor; }
	const PxVec3 &getPickPos() { return mPickPos; }
	const PxVec3 &getPickLocalPos() { return mPickLocalPos; }

	// callback interface

	void onContactNotify(unsigned int arraySizes, void ** shape0Array, void ** shape1Array, void ** actor0Array, void ** actor1Array, float * positionArray, float * normalArray);
	void onConstraintBreak(physx::PxConstraintInfo* constraints, uint32_t count);
	void onWake(PxActor** actors, uint32_t count);
	void onSleep(PxActor** actors, uint32_t count);
	void onTrigger(physx::PxTriggerPair* pairs, uint32_t count);
	void onContact(const physx::PxContactPairHeader& pairHeader, const physx::PxContactPair* pairs, uint32_t nbPairs);
	void onAdvance(const PxRigidBody*const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count);

	void toggleRenderDebris() {mRenderDebris = !mRenderDebris;}
	bool getRenderDebrs() {return mRenderDebris;}
	//virtual void dumpSceneGeometry() {}
	nvidia::Array<PxVec3>& getDebugPoints();
	//virtual void createRenderBuffers() {}
	//void loadAndCreateTextureArrays();

	nvidia::Array<PxVec3>& getCrackNormals() {return crackNormals;}
	nvidia::Array<PxVec3>& getTmpPoints() {return tmpPoints;}

	bool mapShapeToConvex(const PxShape& shape, Convex& convex);
	bool unmapShape(const PxShape& shape);
	Convex* findConvexForShape(const PxShape& shape);
	bool owns(const PxShape& shape) {return NULL != findConvexForShape(shape);}

protected:
	//virtual void create3dTexture() {}
	//virtual void updateConvexesTex() {}
	//void playShatterSound(float objectSize);

	void addActor(Actor* a);  // done internally upon creation

	PxPhysics *mPxPhysics;
	PxCooking *mPxCooking;
	PxScene *mScene;
	const char *mResourcePath;
	bool mPlaySounds;

	//nvidia::Array<Compound*> mCompounds;
	nvidia::Array<Actor*> mActors;

	float mFractureForceThreshold;
	float mContactImpactRadius;

	nvidia::Array<physx::PxContactPairPoint> mContactPoints;
	struct FractureEvent {
		void init() {
			compound = NULL; 
			pos = normal = PxVec3(0.0f, 0.0f, 0.0f); 
			additionalRadialImpulse = additionalNormalImpulse = 0.0f;
			withStatic = false;
		}
		Compound *compound;
		PxVec3 pos;
		PxVec3 normal;
		float additionalRadialImpulse;
		float additionalNormalImpulse;
		bool withStatic;
	};
	nvidia::Array<FractureEvent> mFractureEvents;
	void processFractureEvents(bool& valid,bool* addFireDust = NULL);

	struct RenderBuffers {
		void init() {
			numVertices = 0; numIndices = 0;
			VBO = 0; IBO = 0; matTex = 0; volTex = 0;
			texSize = 0; numConvexes = -1;
		}
		nvidia::Array<float> tmpVertices;
		nvidia::Array<unsigned int> tmpIndices;
		nvidia::Array<float> tmpTexCoords;
		int numVertices, numIndices;
		unsigned int VBO;
		unsigned int IBO;
		unsigned int volTex;
		unsigned int matTex;
		int texSize;
		int numConvexes;
	};
	RenderBuffers mRenderBuffers;
	unsigned int  mSceneVersion;		// changed on each update
	unsigned int  mRenderBufferVersion;	// to handle updates
	unsigned int  mOptixBufferVersion;	// to handle updates

	PxMaterial *mPxDefaultMaterial;

	//ConvexRenderer mConvexRenderer;

	int mNoFractureFrames;
	int mNoSoundFrames;
	int mFrameNr;
	bool mDebugDraw;

	float mPickDepth;
	PxRigidDynamic *mPickActor;
	PxVec3 mPickPos;
	PxVec3 mPickLocalPos;

	float mMinConvexSize;
	int mNumNoFractureFrames; // > 1 to prevent a slow down by too many fracture events

	PxVec3 mCameraPos, mCameraDir, mCameraUp;
	float  mCameraFov;

	float bumpTextureUVScale;
	float extraNoiseScale;
	float roughnessScale;

	float particleBumpTextureUVScale;
	float particleRoughnessScale;
	float particleExtraNoiseScale;
	nvidia::Array<PxVec3> debugPoints;
	bool mRenderDebris;

	PxSimulationEventCallback* mAppNotify;

	//GLuint diffuseTexArray, bumpTexArray, specularTexArray, emissiveReflectSpecPowerTexArray;

	//GLuint loadTextureArray(std::vector<std::string>& names);

	//Singletons
	CompoundCreator* mCompoundCreator;
	Delaunay2d* mDelaunay2d;
	Delaunay3d* mDelaunay3d;
	PolygonTriangulator* mPolygonTriangulator;
	IslandDetector* mIslandDetector;
	MeshClipper* mMeshClipper;

	//Array for use by Compound (effectively static)
	nvidia::Array<PxVec3> crackNormals;
	nvidia::Array<PxVec3> tmpPoints;

	// Deferred Deletion list
	nvidia::Array<Compound*> delCompoundList;

	// Map used to determine SimScene ownership of shape
	shdfnd::HashMap<const PxShape*,Convex*> mShapeMap;
};

}
}
}

#endif
#endif
