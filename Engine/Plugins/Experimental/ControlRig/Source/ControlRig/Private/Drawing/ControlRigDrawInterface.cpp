// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/ControlRigDrawInterface.h"
#include "Math/ControlRigMathLibrary.h"

void FControlRigDrawInterface::DrawPoint(const FTransform& WorldOffset, const FVector& Position, float Size, const FLinearColor& Color)
{
	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::Points, Color, Size, WorldOffset);
	Instruction.Positions.Add(Position);
	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawPoints(const FTransform& WorldOffset, const TArrayView<FVector>& Points, float Size, const FLinearColor& Color)
{
	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::Points, Color, Size, WorldOffset);
	Instruction.Positions.Append(Points.GetData(), Points.Num());
	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawLine(const FTransform& WorldOffset, const FVector& LineStart, const FVector& LineEnd, const FLinearColor& Color, float Thickness)
{
	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::Lines, Color, Thickness, WorldOffset);
	Instruction.Positions.Add(LineStart);
	Instruction.Positions.Add(LineEnd);
	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawLines(const FTransform& WorldOffset, const TArrayView<FVector>& Positions, const FLinearColor& Color, float Thickness)
{
	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::Lines, Color, Thickness, WorldOffset);
	Instruction.Positions.Append(Positions.GetData(), Positions.Num());
}

void FControlRigDrawInterface::DrawLineStrip(const FTransform& WorldOffset, const TArrayView<FVector>& Positions, const FLinearColor& Color, float Thickness)
{
	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::LineStrip, Color, Thickness, WorldOffset);
	Instruction.Positions.Append(Positions.GetData(), Positions.Num());
	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawBox(const FTransform& WorldOffset, const FTransform& Transform, const FLinearColor& Color, float Thickness)
{
	FTransform DrawTransform = Transform * WorldOffset;

	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::Lines, Color, Thickness, DrawTransform);

	Instruction.Positions.Add(FVector(0.5f, 0.5f, 0.5f));
	Instruction.Positions.Add(FVector(0.5f, -0.5f, 0.5f));
	Instruction.Positions.Add(FVector(0.5f, -0.5f, 0.5f));
	Instruction.Positions.Add(FVector(-0.5f, -0.5f, 0.5f));
	Instruction.Positions.Add(FVector(-0.5f, -0.5f, 0.5f));
	Instruction.Positions.Add(FVector(-0.5f, 0.5f, 0.5f));
	Instruction.Positions.Add(FVector(-0.5f, 0.5f, 0.5f));
	Instruction.Positions.Add(FVector(0.5f, 0.5f, 0.5f));

	Instruction.Positions.Add(FVector(0.5f, 0.5f, -0.5f));
	Instruction.Positions.Add(FVector(0.5f, -0.5f, -0.5f));
	Instruction.Positions.Add(FVector(0.5f, -0.5f, -0.5f));
	Instruction.Positions.Add(FVector(-0.5f, -0.5f, -0.5f));
	Instruction.Positions.Add(FVector(-0.5f, -0.5f, -0.5f));
	Instruction.Positions.Add(FVector(-0.5f, 0.5f, -0.5f));
	Instruction.Positions.Add(FVector(-0.5f, 0.5f, -0.5f));
	Instruction.Positions.Add(FVector(0.5f, 0.5f, -0.5f));

	Instruction.Positions.Add(FVector(0.5f, 0.5f, 0.5f));
	Instruction.Positions.Add(FVector(0.5f, 0.5f, -0.5f));
	Instruction.Positions.Add(FVector(0.5f, -0.5f, 0.5f));
	Instruction.Positions.Add(FVector(0.5f, -0.5f, -0.5f));
	Instruction.Positions.Add(FVector(-0.5f, -0.5f, 0.5f));
	Instruction.Positions.Add(FVector(-0.5f, -0.5f, -0.5f));
	Instruction.Positions.Add(FVector(-0.5f, 0.5f, 0.5f));
	Instruction.Positions.Add(FVector(-0.5f, 0.5f, -0.5f));

	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawAxes(const FTransform& WorldOffset, const FTransform& Transform, float Size, float Thickness)
{
	DrawLine(WorldOffset, Transform.GetLocation(), Transform.TransformPosition(FVector(Size, 0.f, 0.f)), FLinearColor::Red, Thickness);
	DrawLine(WorldOffset, Transform.GetLocation(), Transform.TransformPosition(FVector(0.f, Size, 0.f)), FLinearColor::Green, Thickness);
	DrawLine(WorldOffset, Transform.GetLocation(), Transform.TransformPosition(FVector(0.f, 0.f, Size)), FLinearColor::Blue, Thickness);
}

void FControlRigDrawInterface::DrawRectangle(const FTransform& WorldOffset, const FTransform& Transform, float Size, const FLinearColor& Color, float Thickness)
{
	FTransform DrawTransform = Transform * WorldOffset;

	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::LineStrip, Color, Thickness, DrawTransform);

	float Extent = Size * 0.5f;
	Instruction.Positions.Reserve(5);
	Instruction.Positions.Add(FVector(-Extent, -Extent, 0.f));
	Instruction.Positions.Add(FVector(-Extent, Extent, 0.f));
	Instruction.Positions.Add(FVector(Extent, Extent, 0.f));
	Instruction.Positions.Add(FVector(Extent, -Extent, 0.f));
	Instruction.Positions.Add(FVector(-Extent, -Extent, 0.f));

	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawArc(const FTransform& WorldOffset, const FTransform& Transform, float Radius, float MinimumAngle, float MaximumAngle, const FLinearColor& Color, float Thickness, int32 Detail)
{
	int32 Count = FMath::Clamp<int32>(Detail, 4, 32);
	
	FTransform DrawTransform = Transform * WorldOffset;

	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::LineStrip, Color, Thickness, DrawTransform);
	Instruction.Positions.Reserve(Count);

	FVector V = FVector(Radius, 0.f, 0.f);
	FQuat Rotation(FVector(0.f, 0.f, 1.f), MinimumAngle);
	V = Rotation.RotateVector(V);
	Instruction.Positions.Add(V);
	float StepAngle = (MaximumAngle - MinimumAngle) / float(Count);
	if (FMath::Abs<float>(MaximumAngle - MinimumAngle) >= PI * 2.f - SMALL_NUMBER)
	{
		StepAngle = (PI * 2.f) / float(Count);
		Count++;
	}
	Rotation = FQuat(FVector(0.f, 0.f, 1.f), StepAngle);
	for(int32 Index=1;Index<Count;Index++)
	{
		V = Rotation.RotateVector(V);
		Instruction.Positions.Add(V);
	}

	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawBezier(const FTransform& WorldOffset, const FCRFourPointBezier& InBezier, float MinimumU, float MaximumU, const FLinearColor& Color, float Thickness, int32 Detail)
{
	int32 Count = FMath::Clamp<int32>(Detail, 4, 64);
	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::LineStrip, Color, Thickness, WorldOffset);
	Instruction.Positions.SetNumUninitialized(Count);

	float T = MinimumU;
	float Step = (MaximumU - MinimumU) / float(Detail-1);
	for(int32 Index=0;Index<Count;Index++)
	{
		FVector Tangent;
		FControlRigMathLibrary::FourPointBezier(InBezier, T, Instruction.Positions[Index], Tangent);
		T += Step;
	}

	Instructions.Add(Instruction);
}

void FControlRigDrawInterface::DrawHierarchy(const FTransform& WorldOffset, const FRigBoneHierarchy& Hierarchy, EControlRigDrawHierarchyMode::Type Mode, float Scale, const FLinearColor& Color, float Thickness)
{
	switch (Mode)
	{
		case EControlRigDrawHierarchyMode::Axes:
		{
			FControlRigDrawInstruction InstructionX(EControlRigDrawSettings::Lines, FLinearColor::Red, Thickness, WorldOffset);
			FControlRigDrawInstruction InstructionY(EControlRigDrawSettings::Lines, FLinearColor::Green, Thickness, WorldOffset);
			FControlRigDrawInstruction InstructionZ(EControlRigDrawSettings::Lines, FLinearColor::Blue, Thickness, WorldOffset);
			FControlRigDrawInstruction InstructionParent(EControlRigDrawSettings::Lines, Color, Thickness, WorldOffset);
			InstructionX.Positions.Reserve(Hierarchy.Num() * 2);
			InstructionY.Positions.Reserve(Hierarchy.Num() * 2);
			InstructionZ.Positions.Reserve(Hierarchy.Num() * 2);
			InstructionParent.Positions.Reserve(Hierarchy.Num() * 6);

			for (const FRigBone& Bone : Hierarchy)
			{
				FTransform Transform = Bone.GlobalTransform;
				FVector P0 = Transform.GetLocation();
				FVector PX = Transform.TransformPosition(FVector(Scale, 0.f, 0.f));
				FVector PY = Transform.TransformPosition(FVector(0.f, Scale, 0.f));
				FVector PZ = Transform.TransformPosition(FVector(0.f, 0.f, Scale));
				InstructionX.Positions.Add(P0);
				InstructionX.Positions.Add(PX);
				InstructionY.Positions.Add(P0);
				InstructionY.Positions.Add(PY);
				InstructionZ.Positions.Add(P0);
				InstructionZ.Positions.Add(PZ);

				if (Bone.ParentIndex != INDEX_NONE)
				{
					FTransform ParentTransform = Hierarchy[Bone.ParentIndex].GlobalTransform;
					FVector P1 = ParentTransform.GetLocation();
					InstructionParent.Positions.Add(P0);
					InstructionParent.Positions.Add(P1);
				}
			}

			Instructions.Add(InstructionX);
			Instructions.Add(InstructionY);
			Instructions.Add(InstructionZ);
			Instructions.Add(InstructionParent);
			break;
		}
	}
}

void FControlRigDrawInterface::DrawPointSimulation(const FTransform& WorldOffset, const FCRSimPointContainer& Simulation, const FLinearColor& Color, float Thickness, float PrimitiveSize, bool bDrawPointsAsSphere)
{
	FControlRigDrawInstruction PointsInstruction(EControlRigDrawSettings::Points, Color, Thickness * 6.f, WorldOffset);
	FControlRigDrawInstruction SpringsInstruction(EControlRigDrawSettings::Lines, Color * FLinearColor(0.55f, 0.55f, 0.55f, 1.f), Thickness, WorldOffset);
	FControlRigDrawInstruction VolumesMinInstruction(EControlRigDrawSettings::Lines, Color * FLinearColor(0.25f, 0.25f, 0.25f, 1.f), Thickness, WorldOffset);
	FControlRigDrawInstruction VolumesMaxInstruction(EControlRigDrawSettings::Lines, Color * FLinearColor(0.75f, 0.75f, 0.75f, 1.f) + FLinearColor(0.25f, 0.25f, 0.25f, 0.f), Thickness, WorldOffset);

	if (bDrawPointsAsSphere)
	{
		PointsInstruction.PrimitiveType = EControlRigDrawSettings::Lines;
		PointsInstruction.Thickness = Thickness * 2.f;

		for (int32 PointIndex = 0; PointIndex < Simulation.Points.Num(); PointIndex++)
		{
			FCRSimPoint Point = Simulation.GetPointInterpolated(PointIndex);
			FTransform Transform = FTransform(Point.Position);
			static const int32 Subdivision = 8;
			FVector MinV = Transform.TransformVector(FVector(Point.Size, 0.f, 0.f));
			FQuat Q = FQuat(Transform.TransformVectorNoScale(FVector(0.f, 1.f, 0.f)), 2.f * PI / float(Subdivision));
			for (int32 Iteration = 0; Iteration < Subdivision; Iteration++)
			{
				PointsInstruction.Positions.Add(Transform.GetLocation() + MinV);
				MinV = Q.RotateVector(MinV);
				PointsInstruction.Positions.Add(Transform.GetLocation() + MinV);
			}
			MinV = Transform.TransformVector(FVector(Point.Size, 0.f, 0.f));
			Q = FQuat(Transform.TransformVectorNoScale(FVector(0.f, 0.f, 1.f)), 2.f * PI / float(Subdivision));
			for (int32 Iteration = 0; Iteration < Subdivision; Iteration++)
			{
				PointsInstruction.Positions.Add(Transform.GetLocation() + MinV);
				MinV = Q.RotateVector(MinV);
				PointsInstruction.Positions.Add(Transform.GetLocation() + MinV);
			}
			MinV = Transform.TransformVector(FVector(0.f, Point.Size, 0.f));
			Q = FQuat(Transform.TransformVectorNoScale(FVector(1.f, 0.f, 0.f)), 2.f * PI / float(Subdivision));
			for (int32 Iteration = 0; Iteration < Subdivision; Iteration++)
			{
				PointsInstruction.Positions.Add(Transform.GetLocation() + MinV);
				MinV = Q.RotateVector(MinV);
				PointsInstruction.Positions.Add(Transform.GetLocation() + MinV);
			}
		}
	}
	else
	{
		PointsInstruction.Positions.Reserve(Simulation.Points.Num());
		for (int32 PointIndex = 0; PointIndex < Simulation.Points.Num(); PointIndex++)
		{
			FCRSimPoint Point = Simulation.GetPointInterpolated(PointIndex);
			PointsInstruction.Positions.Add(Point.Position);
		}
	}

	SpringsInstruction.Positions.Reserve(Simulation.Springs.Num() * 2);
	for (const FCRSimLinearSpring& Spring : Simulation.Springs)
	{
		if (Spring.SubjectA == INDEX_NONE || Spring.SubjectB == INDEX_NONE)
		{
			continue;
		}
		if (Spring.Coefficient <= SMALL_NUMBER)
		{
			continue;
		}
		SpringsInstruction.Positions.Add(Simulation.GetPointInterpolated(Spring.SubjectA).Position);
		SpringsInstruction.Positions.Add(Simulation.GetPointInterpolated(Spring.SubjectB).Position);
	}

	if (PrimitiveSize > SMALL_NUMBER)
	{
		for (const FCRSimSoftCollision& Volume : Simulation.CollisionVolumes)
		{
			FTransform Transform = Volume.Transform;
			switch (Volume.ShapeType)
			{
				case ECRSimSoftCollisionType::Plane:
				{
					VolumesMinInstruction.PrimitiveType = EControlRigDrawSettings::LineStrip;
					VolumesMinInstruction.Positions.Add(Transform.TransformPosition(FVector(PrimitiveSize, PrimitiveSize, Volume.MinimumDistance) * 0.5f));
					VolumesMinInstruction.Positions.Add(Transform.TransformPosition(FVector(-PrimitiveSize, PrimitiveSize, Volume.MinimumDistance) * 0.5f));
					VolumesMinInstruction.Positions.Add(Transform.TransformPosition(FVector(-PrimitiveSize, -PrimitiveSize, Volume.MinimumDistance) * 0.5f));
					VolumesMinInstruction.Positions.Add(Transform.TransformPosition(FVector(PrimitiveSize, -PrimitiveSize, Volume.MinimumDistance) * 0.5f));
					VolumesMinInstruction.Positions.Add(Transform.TransformPosition(FVector(PrimitiveSize, PrimitiveSize, Volume.MinimumDistance) * 0.5f));
					VolumesMaxInstruction.PrimitiveType = EControlRigDrawSettings::LineStrip;
					VolumesMaxInstruction.Positions.Add(Transform.TransformPosition(FVector(PrimitiveSize, PrimitiveSize, Volume.MaximumDistance) * 0.5f));
					VolumesMaxInstruction.Positions.Add(Transform.TransformPosition(FVector(-PrimitiveSize, PrimitiveSize, Volume.MaximumDistance) * 0.5f));
					VolumesMaxInstruction.Positions.Add(Transform.TransformPosition(FVector(-PrimitiveSize, -PrimitiveSize, Volume.MaximumDistance) * 0.5f));
					VolumesMaxInstruction.Positions.Add(Transform.TransformPosition(FVector(PrimitiveSize, -PrimitiveSize, Volume.MaximumDistance) * 0.5f));
					VolumesMaxInstruction.Positions.Add(Transform.TransformPosition(FVector(PrimitiveSize, PrimitiveSize, Volume.MaximumDistance) * 0.5f));
					break;
				}
				case ECRSimSoftCollisionType::Sphere:
				{
					static const int32 Subdivision = 8;
					FVector MinV = Transform.TransformVector(FVector(Volume.MinimumDistance, 0.f, 0.f));
					FVector MaxV = Transform.TransformVector(FVector(Volume.MaximumDistance, 0.f, 0.f));
					FQuat Q = FQuat(Transform.TransformVectorNoScale(FVector(0.f, 1.f, 0.f)), 2.f * PI / float(Subdivision));
					for (int32 Iteration = 0; Iteration < Subdivision; Iteration++)
					{
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						MinV = Q.RotateVector(MinV);
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);
						MaxV = Q.RotateVector(MaxV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);

					}
					MinV = Transform.TransformVector(FVector(Volume.MinimumDistance, 0.f, 0.f));
					MaxV = Transform.TransformVector(FVector(Volume.MaximumDistance, 0.f, 0.f));
					Q = FQuat(Transform.TransformVectorNoScale(FVector(0.f, 0.f, 1.f)), 2.f * PI / float(Subdivision));
					for (int32 Iteration = 0; Iteration < Subdivision; Iteration++)
					{
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						MinV = Q.RotateVector(MinV);
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);
						MaxV = Q.RotateVector(MaxV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);

					}
					MinV = Transform.TransformVector(FVector(0.f, Volume.MinimumDistance, 0.f));
					MaxV = Transform.TransformVector(FVector(0.f, Volume.MaximumDistance, 0.f));
					Q = FQuat(Transform.TransformVectorNoScale(FVector(1.f, 0.f, 0.f)), 2.f * PI / float(Subdivision));
					for (int32 Iteration = 0; Iteration < Subdivision; Iteration++)
					{
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						MinV = Q.RotateVector(MinV);
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);
						MaxV = Q.RotateVector(MaxV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);

					}
					break;
				}
				case ECRSimSoftCollisionType::Cone:
				{
					static const int32 Subdivision = 8;
					FVector V = FVector(0.f, 0.f, PrimitiveSize);
					FQuat Q = FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(Volume.MinimumDistance));
					FVector MinV = Q.RotateVector(V);
					MinV = Transform.TransformVector(MinV);
					Q = FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(Volume.MaximumDistance));
					FVector MaxV = Q.RotateVector(V);
					MaxV = Transform.TransformVector(MaxV);
					Q = FQuat(Transform.TransformVectorNoScale(FVector(0.f, 0.f, 1.f)), 2.f * PI / float(Subdivision));
					for (int32 Iteration = 0; Iteration < Subdivision; Iteration++)
					{
						VolumesMinInstruction.Positions.Add(Transform.GetLocation());
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						MinV = Q.RotateVector(MinV);
						VolumesMinInstruction.Positions.Add(Transform.GetLocation() + MinV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation());
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);
						MaxV = Q.RotateVector(MaxV);
						VolumesMaxInstruction.Positions.Add(Transform.GetLocation() + MaxV);
					}
					break;
				}
			}
		}
	}

	Instructions.Add(PointsInstruction);
	if (SpringsInstruction.Positions.Num() > 0)
	{
		Instructions.Add(SpringsInstruction);
	}
	if (VolumesMinInstruction.Positions.Num() > 0)
	{
		Instructions.Add(VolumesMinInstruction);
		Instructions.Add(VolumesMaxInstruction);
	}
}
