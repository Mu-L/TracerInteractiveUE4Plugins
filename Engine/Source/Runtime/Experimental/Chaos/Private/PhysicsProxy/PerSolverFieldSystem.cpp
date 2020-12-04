// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/PerSolverFieldSystem.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Field/FieldSystem.h"
#include "Chaos/PBDRigidClustering.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsSolver.h"
#include "ChaosStats.h"

void ResetIndicesArray(TArray<int32> & IndicesArray, int32 Size)
{
	if(IndicesArray.Num() != Size)
	{
		IndicesArray.SetNum(Size);
		for(int32 i = 0; i < IndicesArray.Num(); ++i)
		{
			IndicesArray[i] = i;
		}
	}
}

//==============================================================================
// FPerSolverFieldSystem
//==============================================================================

template <typename Traits>
void FPerSolverFieldSystem::FieldParameterUpdateCallback(
	Chaos::TPBDRigidsSolver<Traits>* InSolver, 
	Chaos::TPBDRigidParticles<float, 3>& Particles, 
	Chaos::TArrayCollectionArray<float>& Strains, 
	Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, 
	TMap<int32, int32>& PositionTargetedParticles, 
	//const TArray<FKinematicProxy>& AnimatedPosition, 
	const float InTime)
{
	using namespace Chaos;
	SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Object);
	
	Chaos::TPBDRigidsSolver<Traits>* CurrentSolver = InSolver;

	const int32 NumCommands = Commands.Num();
	if (Commands.Num() && InSolver)
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		// @todo: This seems like a waste if we just want to get everything
		//TArray<ContextIndex> IndicesArray;
		TArray<Chaos::TGeometryParticleHandle<float,3>*> Handles;
		TArray<FVector> SamplePoints;
		TArray<ContextIndex> SampleIndices;
		TArray<ContextIndex>& IndicesArray = SampleIndices; // Do away with!
		EFieldResolutionType PrevResolutionType = static_cast<EFieldResolutionType>(0); // none
		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& Command = Commands[CommandIndex];
			const EFieldResolutionType ResolutionType = 
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution) ?
					Command.GetMetaDataAs<FFieldSystemMetaDataProcessingResolution>(
						FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution)->ProcessingResolution :
					EFieldResolutionType::Field_Resolution_Minimal;
			if (PrevResolutionType != ResolutionType || Handles.Num() == 0)
			{
				FPerSolverFieldSystem::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
				PrevResolutionType = ResolutionType;

				SamplePoints.SetNum(Handles.Num());
				SampleIndices.SetNum(Handles.Num());
				for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
				{
					SamplePoints[Idx] = Handles[Idx]->X();
					SampleIndices[Idx] = ContextIndex(Idx, Idx);
				}
			}

			if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicState);

				if (Handles.Num())
				{
					TArrayView<Chaos::TGeometryParticleHandle<float,3>*> HandlesView(&(Handles[0]), Handles.Num());
					TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
					TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

					FFieldContext Context(
						SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
						SamplePointsView,
						Command.MetaData);

					// Sample the dynamic state array in the field

					// #BGTODO We're initializing every particle in the simulation here even though we're probably
					// going to cull it in the field eval - can probably be smarter about this.
					
					TArray<int32> DynamicState; // #BGTODO Enum class support (so we can size the underlying type to be more appropriate)
					DynamicState.AddUninitialized(Handles.Num());					
					int32 i = 0;
					for(Chaos::TGeometryParticleHandle<float,3>* Handle : Handles)
					{
						const Chaos::EObjectStateType ObjectState = Handle->ObjectState();
						switch (ObjectState)
						{
						case Chaos::EObjectStateType::Kinematic:
							DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Kinematic;
							break;
						case Chaos::EObjectStateType::Static:
							DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Static;
							break;
						case Chaos::EObjectStateType::Sleeping:
							DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Sleeping;
							break;
						case Chaos::EObjectStateType::Dynamic:
						case Chaos::EObjectStateType::Uninitialized:
						default:
							DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Dynamic;
							break;
						}
					}

					TArrayView<int32> DynamicStateView(&(DynamicState[0]), DynamicState.Num());
					if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
						TEXT("Field based evaluation of the simulations 'ObjectType' parameter expects int32 field inputs.")))
					{
						static_cast<const FFieldNode<int32>*>(
							Command.RootNode.Get())->Evaluate(Context, DynamicStateView);
					}

					bool StateChanged = false;
					for(const ContextIndex& Index : Context.GetEvaluatedSamples())
					{
						Chaos::TGeometryParticleHandle<float, 3>* Handle = Handles[Index.Sample];

						// Lower level particle handles, like TGeometryParticleHandle and 
						// TKinematicParticleHandle, infer their dynamic state by whether or not
						// promotion to a derived c++ handle type succeeds or fails.
						//
						// THAT IS NOT WHAT WE WANT.
						//
						// PBDRigidParticles has an array of EObjectStateType, and the associated
						// handle has a getter and a setter for that data.  So, at least for now,
						// we're just going to ignore non-dynamic particles.  This has the added
						// benefit of not needing to deal with the floor, as it's pretty likely to
						// not be dynamic.
						if(Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handle->CastToRigidParticle())
						{
							auto SetParticleState = [CurrentSolver](Chaos::TPBDRigidParticleHandle<float, 3>* InHandle, EObjectStateType InState)
							{
								const bool bIsGC = (InHandle->GetParticleType() == Chaos::EParticleType::GeometryCollection) ||
									(InHandle->GetParticleType() == Chaos::EParticleType::Clustered && !InHandle->CastToClustered()->InternalCluster());

								if(!bIsGC)
								{
									CurrentSolver->GetEvolution()->SetParticleObjectState(InHandle, InState);
								}
								else
								{
									InHandle->SetObjectStateLowLevel(InState);
								}
							};

							const EObjectStateType HandleState = RigidHandle->ObjectState();

							const int32 FieldState = DynamicStateView[Index.Result];
							if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic)
							{
								if ((HandleState == Chaos::EObjectStateType::Static ||
									 HandleState == Chaos::EObjectStateType::Kinematic) &&
									RigidHandle->M() > FLT_EPSILON)
								{
									SetParticleState(RigidHandle, EObjectStateType::Dynamic);
									StateChanged = true;
								}
								else if (HandleState == Chaos::EObjectStateType::Sleeping)
								{
									SetParticleState(RigidHandle, EObjectStateType::Dynamic);
									StateChanged = true;
								}
							}
							else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic)
							{
								if (HandleState != Chaos::EObjectStateType::Kinematic)
								{
									SetParticleState(RigidHandle, EObjectStateType::Kinematic);
									RigidHandle->SetV(Chaos::TVector<float, 3>(0));
									RigidHandle->SetW(Chaos::TVector<float, 3>(0));
									StateChanged = true;
								}
							}
							else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Static)
							{
								if (HandleState != Chaos::EObjectStateType::Static)
								{
									SetParticleState(RigidHandle, EObjectStateType::Static);
									RigidHandle->SetV(Chaos::TVector<float, 3>(0));
									RigidHandle->SetW(Chaos::TVector<float, 3>(0));
									StateChanged = true;
								}
							}
							else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Sleeping)
							{
								if (HandleState != Chaos::EObjectStateType::Sleeping)
								{
									SetParticleState(RigidHandle, EObjectStateType::Sleeping);
									StateChanged = true;
								}
							}
						} // handle is dynamic
					} // end for all samples

					if (StateChanged)
					{
						// regenerate views
						CurrentSolver->GetParticles().UpdateGeometryCollectionViews(true);
					}

					const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = CurrentSolver->GetParticles();
					auto& Clustering = CurrentSolver->GetEvolution()->GetRigidClustering();
					const auto& ClusterMap = Clustering.GetChildrenMap();

					const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
						SolverParticles.GetNonDisabledView();

					for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
						It != ItEnd; ++It)
					{
						const auto* Clustered = It->Handle()->CastToClustered();
						if (Clustered && Clustered->ClusterIds().NumChildren)
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = It->Handle()->CastToRigidParticle();
							check(RigidHandle);
							Clustering.UpdateKinematicProperties(RigidHandle);
						}
					}

				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ActivateDisabled))
			{
				if (Handles.Num())
				{
					TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
					TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

					FFieldContext Context{
						SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
						SamplePointsView,
						Command.MetaData
					};

					//
					//  Sample the dynamic state array in the field
					//
					TArray<int32> DynamicState;
					DynamicState.AddUninitialized(Particles.Size());
					for(const ContextIndex& Index : IndicesArray)
					{
						DynamicState[Index.Sample] = 0;	//is this needed?
					}
					
					for (const ContextIndex& Index : IndicesArray)
					{
						const int32 i = Index.Sample;
						if (Particles.Disabled(i))
						{
							DynamicState[i] = 1;
						}
						else
						{
							DynamicState[i] = 0;
						}
					}
					TArrayView<int32> DynamicStateView(&(DynamicState[0]), DynamicState.Num());

					if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
						TEXT("Field based evaluation of the simulations 'ObjectType' parameter expects int32 field inputs.")))
					{
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, DynamicStateView);
					}

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
					// transfer results to rigid system.
					for(const ContextIndex& Index : Context.GetEvaluatedSamples())
					{
						int32 RigidBodyIndex = Index.Result;
						if(DynamicStateView[RigidBodyIndex] == 0 && Particles.Disabled(RigidBodyIndex))
						{
							ensure(CurrentSolver->GetRigidClustering().GetClusterIdsArray()[RigidBodyIndex].Id == INDEX_NONE);
							CurrentSolver->GetEvolution()->EnableParticle(RigidBodyIndex, INDEX_NONE);
							Particles.SetObjectState(RigidBodyIndex, Chaos::EObjectStateType::Dynamic);
						}
					}
#endif
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_ExternalClusterStrain);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Strain' parameter expects float field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						// TODO: Chaos, Ryan
						// As we're allocating a buffer the size of all particles every iteration, 
						// I suspect this is a performance hit.  It seems like we should add a buffer
						// that's recycled rather than reallocating.  It could live on the particles
						// and its lifetime tied to an object that lives in the scope of the object 
						// that's driving the sampling of the field.

						TArray<float> StrainSamples;
						// There's 2 ways to think about initializing this array...
						// Either we have a low number of indices, and the cost of iterating
						// over the indices in addition to StrainSamples is lower than the
						// cost of initializing them all, or it's cheaper and potentially 
						// more cache coherent to just initialize them all.  I'm thinking
						// the latter may be more likely...
						StrainSamples.AddZeroed(SamplePointsView.Num());

						TArrayView<float> FloatBuffer(&StrainSamples[0], StrainSamples.Num());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, FloatBuffer);

						int32 Iterations = 1;
						if (Command.MetaData.Contains(FFieldSystemMetaData::EMetaType::ECommandData_Iteration))
						{
							Iterations = static_cast<FFieldSystemMetaDataIteration*>(
								Command.MetaData[FFieldSystemMetaData::EMetaType::ECommandData_Iteration].Get())->Iterations;
						}

						if (StrainSamples.Num())
						{
							TMap<TGeometryParticleHandle<float, 3>*, float> Map;
							
							for(const ContextIndex& Index : Context.GetEvaluatedSamples())
							{
								if(StrainSamples[Index.Result] > 0)
								{
									Map.Add(Handles[Index.Sample], StrainSamples[Index.Result]);
								}

							}

							// Capture the results from the breaking model to post-process
							TMap<TPBDRigidClusteredParticleHandle<FReal, 3>*, TSet<TPBDRigidParticleHandle<FReal, 3>*>> BreakResults = CurrentSolver->GetEvolution()->GetRigidClustering().BreakingModel(&Map);
							
							// If clusters broke apart then we'll have activated new particles that have no relationship to the proxy that now owns them
							// Here we attach each new particle to the proxy of the parent particle that owns it.
							for(const TPair<TPBDRigidClusteredParticleHandle<FReal, 3>*, TSet<TPBDRigidParticleHandle<FReal, 3>*>>& Iter : BreakResults)
							{
								const TSet<TPBDRigidParticleHandle<FReal, 3>*>& Activated = Iter.Value;

								for(TPBDRigidParticleHandle<FReal, 3>* Handle : Activated)
								{
									if(!CurrentSolver->GetProxies(Handle))
									{
										const TSet<IPhysicsProxyBase*> * ParentProxies = CurrentSolver->GetProxies(Iter.Key);
										if(ensure(ParentProxies))
										{
											for( IPhysicsProxyBase* ParentProxy : *ParentProxies)
												CurrentSolver->AddParticleToProxy(Handle, ParentProxy);
										}
									}
								}
							}
						}
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_Kill))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Kill);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Disabled' parameter expects float field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						TArray<float> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());
						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(
							Context, ResultsView);
						
						bool HasDisabled = false;
						for(const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							
							if (RigidHandle && ResultsView[Index.Result] > 0.0)
							{
								CurrentSolver->GetEvolution()->DisableParticle(RigidHandle);
								HasDisabled = true;
							}
						}
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_LinearVelocity);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'LinearVelocity' parameter expects FVector field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());
						FFieldContext Context(
							SampleIndicesView,
							SamplePointsView,
							Command.MetaData);

						TArray<FVector> LocalResults;
						LocalResults.AddUninitialized(Handles.Num());
						TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for(const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if(RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
							{
								RigidHandle->V() += ResultsView[Index.Result];
							}
						}
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularVelociy))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_AngularVelocity);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'AngularVelocity' parameter expects FVector field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						FVector * vptr = &(Particles.W(0));
						TArrayView<FVector> ResultsView(vptr, Particles.Size());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_SleepingThreshold))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_SleepingThreshold);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Disable' parameter expects scale field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());
						FFieldContext Context(
							SampleIndicesView,
							SamplePointsView,
							Command.MetaData);

						TArray<float> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for(const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();

							if (RigidHandle && ResultsView.Num() > 0)
							{
								// if no per particle physics material is set, make one
								if (!CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle).IsValid())
								{

									TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>();
									NewMaterial->SleepingLinearThreshold = ResultsView[Index.Result];
									NewMaterial->SleepingAngularThreshold = ResultsView[Index.Result];


									CurrentSolver->GetEvolution()->SetPhysicsMaterial(RigidHandle, MakeSerializable(NewMaterial));
									CurrentSolver->GetEvolution()->SetPerParticlePhysicsMaterial(RigidHandle, NewMaterial);
								}
								else
								{
									const TUniquePtr<FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle);

									if (ResultsView[Index.Result] != InstanceMaterial->DisabledLinearThreshold)
									{
										InstanceMaterial->SleepingLinearThreshold = ResultsView[Index.Result];
										InstanceMaterial->SleepingAngularThreshold = ResultsView[Index.Result];
									}
								}
							}
						}
					}				
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DisableThreshold))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DisableThreshold);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Disable' parameter expects scale field inputs.")))
				{
					
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context(SampleIndicesView, SamplePointsView, Command.MetaData);

						TArray<float> LocalResults;
						LocalResults.AddUninitialized(Handles.Num());

						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());
						
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for(const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							
							if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic && ResultsView.Num() > 0)
							{						
								// if no per particle physics material is set, make one
								if (!CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle).IsValid())
								{

									TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>();
									NewMaterial->DisabledLinearThreshold = ResultsView[Index.Result];
									NewMaterial->DisabledAngularThreshold = ResultsView[Index.Result];

									CurrentSolver->GetEvolution()->SetPhysicsMaterial(RigidHandle, MakeSerializable(NewMaterial));
									CurrentSolver->GetEvolution()->SetPerParticlePhysicsMaterial(RigidHandle, NewMaterial);
								} 
								else
								{
									const TUniquePtr<FChaosPhysicsMaterial> &InstanceMaterial = CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle);
		
									if (ResultsView[Index.Result] != InstanceMaterial->DisabledLinearThreshold)
									{
										InstanceMaterial->DisabledLinearThreshold = ResultsView[Index.Result];
										InstanceMaterial->DisabledAngularThreshold = ResultsView[Index.Result];
									}
								}
							}
						}
					}									
				}

				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_InternalClusterStrain))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_InternalClusterStrain);
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'ExternalClusterStrain' parameter expects scalar field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						TArray<float> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidClusteredParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToClustered();
							if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
							{
								RigidHandle->Strain() += ResultsView[Index.Result];
							}
						}

					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_CollisionGroup))
			{
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
					TEXT("Field based evaluation of the simulations 'CollisionGroup' parameter expects int field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						int32 * cptr = &(Particles.CollisionGroup(0));
						TArrayView<int32> ResultsView(cptr, Particles.Size());
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionStatic))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionStatic);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Position' parameter expects integer field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						TArray<int32> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& Index : IndicesArray)
						{
							Results[Index.Sample] = false;
						}
						TArrayView<int32> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

#if TODO_REIMPLEMENT_KINEMATIC_PROXY
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (Results[i])
							{
								if (PositionTargetedParticles.Contains(i))
								{
									int32 Index = PositionTargetedParticles[i];
									PositionTarget.Replace(Index, Particles.X(i));
								}
								else
								{
									int32 Index = PositionTarget.AddConstraint(Particles.Handle(i), Particles.X(i)); //??
									PositionTargetedParticles.Add(i, Index);
								}
							}
						}
#endif
					}
				}

				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionTarget))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionTarget);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'PositionTarget' parameter expects vector field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						TArray<FVector> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& Index : IndicesArray)
						{
							Results[Index.Sample] = FVector(FLT_MAX);
						}
						TArrayView<FVector> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

#if TODO_REIMPLEMENT_KINEMATIC_PROXY
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (Results[i] != FVector(FLT_MAX))
							{
								if (PositionTargetedParticles.Contains(i))
								{
									int32 Index = PositionTargetedParticles[i];
									PositionTarget.Replace(Index, Results[i]);
								}
								else
								{
									int32 Index = PositionTarget.Add(i, Results[i]);
									PositionTargetedParticles.Add(i, Index);
								}
							}
						}
#endif
					}
				}

				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionAnimated))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionAnimated);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Position' parameter expects integer field inputs.")))
				{
					if (Handles.Num())
					{
						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context{
							SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
							SamplePointsView,
							Command.MetaData
						};

						TArray<int32> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& Index : IndicesArray)
						{
							Results[Index.Sample] = false;
						}
						TArrayView<int32> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

#if TODO_REIMPLEMENT_KINEMATIC_PROXY
						for (int32 i = 0; i < AnimatedPosition.Num(); ++i)
						{
							for (int32 j = 0; j < AnimatedPosition[i].Ids.Num(); ++j)
							{
								int32 Index = AnimatedPosition[i].Ids[j];
								if (Results[Index])
								{
									if (PositionTargetedParticles.Contains(Index))
									{
										int32 PosIndex = PositionTargetedParticles[i];
										PositionTarget.Replace(PosIndex, AnimatedPosition[i].Position[j]);
									}
									else
									{
										int32 PosIndex = PositionTarget.Add(i, AnimatedPosition[i].Position[j]);
										PositionTargetedParticles.Add(i, PosIndex);
									}
								}
							}
						}
#endif
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicConstraint))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicConstraint);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'DynamicConstraint' parameter expects scalar field inputs.")))
				{
#if TODO_REIMPLEMENT_DYNAMIC_CONSTRAINT_ACCESSORS
					Chaos::TPBDRigidDynamicSpringConstraints<float, 3>& DynamicConstraints = FPhysicsSolver::FAccessor(CurrentSolver).DynamicConstraints();
					TSet<int32>& DynamicConstraintParticles = FPhysicsSolver::FAccessor(CurrentSolver).DynamicConstraintParticles();

					FPerSolverFieldSystem::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<float> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& CIndex : IndicesArray)
						{
							Results[CIndex.Sample] = FLT_MAX;
						}
						TArrayView<float> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (Results[i] != FLT_MAX)
							{
								if (!DynamicConstraintParticles.Contains(i))
								{
									DynamicConstraints.SetDistance(Results[i]);
									for (const int32 Index : DynamicConstraintParticles)
									{
										DynamicConstraints.Add(Index, i);
									}
									DynamicConstraintParticles.Add(i);
								}
							}
						}
					}
#endif
				}
				CommandsToRemove.Add(CommandIndex);
			}
		}
		for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
		{
			Commands.RemoveAt(CommandsToRemove[Index]);
		}
	}
}

template <typename Traits>
void FPerSolverFieldSystem::FieldForcesUpdateCallback(
	Chaos::TPBDRigidsSolver<Traits>* InSolver, 
	Chaos::TPBDRigidParticles<float, 3>& Particles, 
	Chaos::TArrayCollectionArray<FVector> & Force, 
	Chaos::TArrayCollectionArray<FVector> & Torque, 
	const float Time)
{
	const int32 NumCommands = Commands.Num();
	if (NumCommands && InSolver)
	{
		Chaos::TPBDRigidsSolver<Traits>* CurrentSolver = InSolver;
		TArray<ContextIndex> IndicesArray;

		TArray<int32> CommandsToRemove;
		for(int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand & Command = Commands[CommandIndex];
			const EFieldResolutionType ResolutionType = 
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution) ?
					Command.GetMetaDataAs<FFieldSystemMetaDataProcessingResolution>(
						FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution)->ProcessingResolution :
					EFieldResolutionType::Field_Resolution_Minimal;

			if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce))
			{
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Force' parameter expects FVector field inputs.")))
				{
					TArray<Chaos::TGeometryParticleHandle<float,3>*> Handles;
					FPerSolverFieldSystem::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
					if (Handles.Num())
					{
						TArray<FVector> SamplePoints;
						TArray<ContextIndex> SampleIndices;
						SamplePoints.AddUninitialized(Handles.Num());
						SampleIndices.AddUninitialized(Handles.Num());
						for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
						{
							SamplePoints[Idx] = Handles[Idx]->X();
							SampleIndices[Idx] = ContextIndex(Idx, Idx);
						}

						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());
						FFieldContext Context(
							SampleIndicesView,
							SamplePointsView,
							Command.MetaData);

						TArray<FVector> LocalForce;
						LocalForce.AddZeroed(Handles.Num());					
						TArrayView<FVector> ForceView(&(LocalForce[0]), LocalForce.Num());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ForceView);
		
						for(const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();

							if(RigidHandle && !RigidHandle->Disabled() && (RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic || RigidHandle->ObjectState() == Chaos::EObjectStateType::Sleeping))
							{				
								if (RigidHandle->Sleeping())
								{
									RigidHandle->SetObjectState(Chaos::EObjectStateType::Dynamic);
								}

								RigidHandle->F() += ForceView[Index.Result];
							}
						}

#if TODO_REIMPLEMENT_WAKE_ISLANDS
						// @todo(ccaulfield): encapsulation: add WakeParticles (and therefore islands) functionality to Evolution
						TSet<int32> IslandsToActivate;
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (ForceView[i] != FVector(0) && Particles.ObjectState(i) == Chaos::EObjectStateType::Sleeping && !Particles.Disabled(i) && IslandsToActivate.Find(Particles.Island(i)) == nullptr)
							{
								IslandsToActivate.Add(Particles.Island(i));
							}
						}
						InSolver->WakeIslands(IslandsToActivate);
#endif
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularTorque))
			{
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Torque' parameter expects FVector field inputs.")))
				{
					TArray<Chaos::TGeometryParticleHandle<float,3>*> Handles;
					FPerSolverFieldSystem::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
					if (Handles.Num())
					{
						TArray<FVector> SamplePoints;
						TArray<ContextIndex> SampleIndices;
						SamplePoints.AddUninitialized(Handles.Num());
						SampleIndices.AddUninitialized(Handles.Num());
						for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
						{
							SamplePoints[Idx] = Handles[Idx]->X();
							SampleIndices[Idx] = ContextIndex(Idx, Idx);
						}

						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context(
							SampleIndicesView,
							SamplePointsView,
							Command.MetaData);

						TArray<FVector> LocalTorque;
						LocalTorque.AddUninitialized(Handles.Num());					
						TArrayView<FVector> TorqueView(&(LocalTorque[0]), LocalTorque.Num());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, TorqueView);
		
						for(const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if(RigidHandle && (RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic || RigidHandle->ObjectState() == Chaos::EObjectStateType::Sleeping))
							{
								if (RigidHandle->Sleeping())
								{
									RigidHandle->SetObjectState(Chaos::EObjectStateType::Dynamic);
								}
								RigidHandle->Torque() += TorqueView[Index.Result];
							}
						}

#if TODO_REIMPLEMENT_WAKE_ISLANDS
						// @todo(ccaulfield): encapsulation: add WakeParticles (and therefore islands) functionality to Evolution
						TSet<int32> IslandsToActivate;
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (TorqueView[i] != FVector(0) && Particles.ObjectState(i) == Chaos::EObjectStateType::Sleeping && !Particles.Disabled(i) && IslandsToActivate.Find(Particles.Island(i)) == nullptr)
							{
								IslandsToActivate.Add(Particles.Island(i));
							}
						}
						InSolver->WakeIslands(IslandsToActivate);
#endif
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
		}
		for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
		{
			Commands.RemoveAt(CommandsToRemove[Index]);
		}
	}
}

void FPerSolverFieldSystem::BufferCommand(const FFieldSystemCommand& InCommand)
{
	Commands.Add(InCommand);
}

//void FPerSolverFieldSystem::GetParticleHandles(
//	TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,
//	const Chaos::FPhysicsSolver* RigidSolver,
//	const EFieldResolutionType ResolutionType,
//	const bool bForce)
//{
//	Handles.SetNum(0, false);
//	if (!bForce)
//		return;
//
//	const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = RigidSolver->GetParticles();
//
//	if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
//	{
//		// const TParticleView<TGeometryParticles<T, d>>& TPBDRigidSOAs<T,d>::GetAllParticlesView()
//		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>> &ParticleView = 
//			SolverParticles.GetAllParticlesView();
//		Handles.Reserve(ParticleView.Num());
//
//		// TParticleIterator<TSOA> Begin() const, TSOA = TGeometryParticles<T, d>
//		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
//			It != ItEnd; ++It)
//		{
//			const Chaos::TTransientGeometryParticleHandle<float,3> *Handle = &(*It);
//			// PBDRigidsSOAs.h only has a const version of GetAllParticlesView() - is that wrong?
//			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float,3>*>(Handle)));
//		}
//	}
//	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
//	{
//		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>> &ParticleView = 
//			SolverParticles.GetNonDisabledView();
//		Handles.Reserve(ParticleView.Num());
//		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
//			It != ItEnd; ++It)
//		{
//			const Chaos::TTransientGeometryParticleHandle<float,3> *Handle = &(*It);
//			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float,3>*>(Handle)));
//		}
//	}
//	else if (ResolutionType == EFieldResolutionType::Field_Resolution_DisabledParents)
//	{
//		check(false); // unimplemented
//	}
//}

template <typename Traits>
void FPerSolverFieldSystem::GetParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
	const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
	const EFieldResolutionType ResolutionType, 
	const bool bForce)
{
	Handles.SetNum(0, false);
	if (!bForce)
	{
		return;
	}

	const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = RigidSolver->GetParticles();

	if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
	{
		const auto& Clustering = RigidSolver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
			SolverParticles.GetNonDisabledView();
		Handles.Reserve(ParticleView.Num()); // ?? what about additional number of children added
		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));

			const auto* Clustered = Handle->CastToClustered();
			if (Clustered && Clustered->ClusterIds().NumChildren)
			{
				Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = (*It).Handle()->CastToRigidParticle();
				if (ClusterMap.Contains(RigidHandle))
				{
					for (Chaos::TPBDRigidParticleHandle<float, 3>* Child : ClusterMap[RigidHandle])
					{
						Handles.Add(Child);
					}
				}
			}
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_DisabledParents)
	{
		const auto& Clustering = RigidSolver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();
		Handles.Reserve(Clustering.GetTopLevelClusterParents().Num());

		for (Chaos::TPBDRigidClusteredParticleHandle<float, 3>* TopLevelParent : Clustering.GetTopLevelClusterParents())
		{
			Handles.Add(TopLevelParent);
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
	{
		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
			SolverParticles.GetAllParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));
		}
	}
}

#define EVOLUTION_TRAIT(Traits)\
template void FPerSolverFieldSystem::FieldParameterUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver, \
		Chaos::TPBDRigidParticles<float, 3>& InParticles, \
		Chaos::TArrayCollectionArray<float>& Strains, \
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, \
		TMap<int32, int32>& PositionTargetedParticles, \
		const float InTime);\
\
template void FPerSolverFieldSystem::FieldForcesUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver, \
		Chaos::TPBDRigidParticles<float, 3>& Particles, \
		Chaos::TArrayCollectionArray<FVector> & Force, \
		Chaos::TArrayCollectionArray<FVector> & Torque, \
		const float Time);\
\
template void FPerSolverFieldSystem::GetParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldResolutionType ResolutionType,\
		const bool bForce);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT