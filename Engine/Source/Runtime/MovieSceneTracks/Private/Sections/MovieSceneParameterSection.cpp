// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneParameterSection.h"
#include "UObject/SequencerObjectVersion.h"
#include "Channels/MovieSceneChannelProxy.h"

FScalarParameterNameAndCurve::FScalarParameterNameAndCurve( FName InParameterName )
{
	ParameterName = InParameterName;
}

FBoolParameterNameAndCurve::FBoolParameterNameAndCurve(FName InParameterName)
{
	ParameterName = InParameterName;
}

FVector2DParameterNameAndCurves::FVector2DParameterNameAndCurves( FName InParameterName )
{
	ParameterName = InParameterName;
}

FVectorParameterNameAndCurves::FVectorParameterNameAndCurves(FName InParameterName)
{
	ParameterName = InParameterName;
}

FColorParameterNameAndCurves::FColorParameterNameAndCurves( FName InParameterName )
{
	ParameterName = InParameterName;
}

FTransformParameterNameAndCurves::FTransformParameterNameAndCurves(FName InParameterName)
{
	ParameterName = InParameterName;
}

UMovieSceneParameterSection::UMovieSceneParameterSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	bSupportsInfiniteRange = true;
	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToRestoreState ? 
			EMovieSceneCompletionMode::KeepState : 
			GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
			EMovieSceneCompletionMode::RestoreState : 
			EMovieSceneCompletionMode::ProjectDefault);
}	

void UMovieSceneParameterSection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		//Don't force if transacting, since it may not be a channel creation/deletion change
		ReconstructChannelProxy(!Ar.IsTransacting());
	}
}

void UMovieSceneParameterSection::PostEditImport()
{
	Super::PostEditImport();

	ReconstructChannelProxy(true);
}


void UMovieSceneParameterSection::ReconstructChannelProxy(bool bForce)
{

	FMovieSceneChannelProxyData Channels;

#if WITH_EDITOR

	for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
	{
		FMovieSceneChannelMetaData MetaData(Scalar.ParameterName, FText::FromName(Scalar.ParameterName));
		// Prevent single channels from collapsing to the track node
		MetaData.bCanCollapseToTrack = false;
		Channels.Add(Scalar.ParameterCurve, MetaData, TMovieSceneExternalValue<float>());
	}

	for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
	{
		FMovieSceneChannelMetaData MetaData(Bool.ParameterName, FText::FromName(Bool.ParameterName));
		// Prevent single channels from collapsing to the track node
		MetaData.bCanCollapseToTrack = false;
		Channels.Add(Bool.ParameterCurve, MetaData, TMovieSceneExternalValue<bool>());
	}
	for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
	{
		FString ParameterString = Vector2D.ParameterName.ToString();
		FText Group = FText::FromString(ParameterString);

		Channels.Add(Vector2D.XCurve, FMovieSceneChannelMetaData(*(ParameterString + TEXT(".X")), FCommonChannelData::ChannelX, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Vector2D.YCurve, FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Y")), FCommonChannelData::ChannelY, Group), TMovieSceneExternalValue<float>());
	}
	for (FVectorParameterNameAndCurves& Vector : GetVectorParameterNamesAndCurves())
	{
		FString ParameterString = Vector.ParameterName.ToString();
		FText Group = FText::FromString(ParameterString);

		Channels.Add(Vector.XCurve, FMovieSceneChannelMetaData(*(ParameterString + TEXT(".X")), FCommonChannelData::ChannelX, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Vector.YCurve, FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Y")), FCommonChannelData::ChannelY, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Vector.ZCurve, FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Z")), FCommonChannelData::ChannelZ, Group), TMovieSceneExternalValue<float>());
	}
	for (FColorParameterNameAndCurves& Color : GetColorParameterNamesAndCurves())
	{
		FString ParameterString = Color.ParameterName.ToString();
		FText Group = FText::FromString(ParameterString);

		FMovieSceneChannelMetaData MetaData_R(*(ParameterString + TEXT("R")), FCommonChannelData::ChannelR, Group);
		MetaData_R.SortOrder = 0;
		MetaData_R.Color = FCommonChannelData::RedChannelColor;

		FMovieSceneChannelMetaData MetaData_G(*(ParameterString + TEXT("G")), FCommonChannelData::ChannelG, Group);
		MetaData_G.SortOrder = 1;
		MetaData_G.Color = FCommonChannelData::GreenChannelColor;

		FMovieSceneChannelMetaData MetaData_B(*(ParameterString + TEXT("B")), FCommonChannelData::ChannelB, Group);
		MetaData_B.SortOrder = 2;
		MetaData_B.Color = FCommonChannelData::BlueChannelColor;

		FMovieSceneChannelMetaData MetaData_A(*(ParameterString + TEXT("A")), FCommonChannelData::ChannelA, Group);
		MetaData_A.SortOrder = 3;

		Channels.Add(Color.RedCurve, MetaData_R, TMovieSceneExternalValue<float>());
		Channels.Add(Color.GreenCurve, MetaData_G, TMovieSceneExternalValue<float>());
		Channels.Add(Color.BlueCurve, MetaData_B, TMovieSceneExternalValue<float>());
		Channels.Add(Color.AlphaCurve, MetaData_A, TMovieSceneExternalValue<float>());
	}

	for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
	{
		FString ParameterString = Transform.ParameterName.ToString();
		FText Group = FText::FromString(ParameterString);

		Channels.Add(Transform.Translation[0], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Translation.X")), FCommonChannelData::ChannelX, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Transform.Translation[1], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Translation.Y")), FCommonChannelData::ChannelY, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Transform.Translation[2], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Translation.Z")), FCommonChannelData::ChannelZ, Group), TMovieSceneExternalValue<float>());

		Channels.Add(Transform.Rotation[0], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Rotation.X")), FCommonChannelData::ChannelX, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Transform.Rotation[1], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Rotation.Y")), FCommonChannelData::ChannelY, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Transform.Rotation[2], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Rotation.Z")), FCommonChannelData::ChannelZ, Group), TMovieSceneExternalValue<float>());

		Channels.Add(Transform.Scale[0], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Scale.X")), FCommonChannelData::ChannelX, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Transform.Scale[1], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Scale.Y")), FCommonChannelData::ChannelY, Group), TMovieSceneExternalValue<float>());
		Channels.Add(Transform.Scale[2], FMovieSceneChannelMetaData(*(ParameterString + TEXT(".Scale.Z")), FCommonChannelData::ChannelZ, Group), TMovieSceneExternalValue<float>());

	}
#else

	for (FScalarParameterNameAndCurve& Scalar : GetScalarParameterNamesAndCurves())
	{
		Channels.Add(Scalar.ParameterCurve);
	}
	for (FBoolParameterNameAndCurve& Bool : GetBoolParameterNamesAndCurves())
	{
		Channels.Add(Bool.ParameterCurve);
	}
	for (FVector2DParameterNameAndCurves& Vector2D : GetVector2DParameterNamesAndCurves())
	{
		Channels.Add(Vector2D.XCurve);
		Channels.Add(Vector2D.YCurve);
	}
	for (FColorParameterNameAndCurves& Color : GetColorParameterNamesAndCurves())
	{
		Channels.Add(Color.RedCurve);
		Channels.Add(Color.GreenCurve);
		Channels.Add(Color.BlueCurve);
		Channels.Add(Color.AlphaCurve);
	}

	for (FTransformParameterNameAndCurves& Transform : GetTransformParameterNamesAndCurves())
	{
		Channels.Add(Transform.Translation[0]);
		Channels.Add(Transform.Translation[1]);
		Channels.Add(Transform.Translation[2]);

		Channels.Add(Transform.Rotation[0]);
		Channels.Add(Transform.Rotation[1]);
		Channels.Add(Transform.Rotation[2]);

		Channels.Add(Transform.Scale[0]);
		Channels.Add(Transform.Scale[1]);
		Channels.Add(Transform.Scale[2]);

	}

#endif

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
	
}

void UMovieSceneParameterSection::AddScalarParameterKey( FName InParameterName, FFrameNumber InTime, float InValue )
{
	FMovieSceneFloatChannel* ExistingChannel = nullptr;
	for ( FScalarParameterNameAndCurve& ScalarParameterNameAndCurve : ScalarParameterNamesAndCurves )
	{
		if ( ScalarParameterNameAndCurve.ParameterName == InParameterName )
		{
			ExistingChannel = &ScalarParameterNameAndCurve.ParameterCurve;
			break;
		}
	}
	if ( ExistingChannel == nullptr )
	{
		const int32 NewIndex = ScalarParameterNamesAndCurves.Add( FScalarParameterNameAndCurve( InParameterName ) );
		ExistingChannel = &ScalarParameterNamesAndCurves[NewIndex].ParameterCurve;

		ReconstructChannelProxy(true);
	}

	ExistingChannel->AddCubicKey(InTime, InValue);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}

void UMovieSceneParameterSection::AddBoolParameterKey(FName InParameterName, FFrameNumber InTime, bool InValue)
{
	FMovieSceneBoolChannel* ExistingChannel = nullptr;
	for (FBoolParameterNameAndCurve& BoolParameterNameAndCurve : BoolParameterNamesAndCurves)
	{
		if (BoolParameterNameAndCurve.ParameterName == InParameterName)
		{
			ExistingChannel = &BoolParameterNameAndCurve.ParameterCurve;
			break;
		}
	}
	if (ExistingChannel == nullptr)
	{
		const int32 NewIndex = BoolParameterNamesAndCurves.Add(FBoolParameterNameAndCurve(InParameterName));
		ExistingChannel = &BoolParameterNamesAndCurves[NewIndex].ParameterCurve;

		ReconstructChannelProxy(true);
	}

	ExistingChannel->GetData().UpdateOrAddKey(InTime, InValue);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}


void UMovieSceneParameterSection::AddVector2DParameterKey(FName InParameterName, FFrameNumber InTime, FVector2D InValue)
{
	FVector2DParameterNameAndCurves* ExistingCurves = nullptr;
	for (FVector2DParameterNameAndCurves& VectorParameterNameAndCurve : Vector2DParameterNamesAndCurves)
	{
		if (VectorParameterNameAndCurve.ParameterName == InParameterName)
		{
			ExistingCurves = &VectorParameterNameAndCurve;
			break;
		}
	}
	if (ExistingCurves == nullptr)
	{
		int32 NewIndex = Vector2DParameterNamesAndCurves.Add(FVector2DParameterNameAndCurves(InParameterName));
		ExistingCurves = &Vector2DParameterNamesAndCurves[NewIndex];

		ReconstructChannelProxy(true);
	}

	ExistingCurves->XCurve.AddCubicKey(InTime, InValue.X);
	ExistingCurves->YCurve.AddCubicKey(InTime, InValue.Y);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}

void UMovieSceneParameterSection::AddVectorParameterKey( FName InParameterName, FFrameNumber InTime, FVector InValue )
{
	FVectorParameterNameAndCurves* ExistingCurves = nullptr;
	for ( FVectorParameterNameAndCurves& VectorParameterNameAndCurve : VectorParameterNamesAndCurves )
	{
		if ( VectorParameterNameAndCurve.ParameterName == InParameterName )
		{
			ExistingCurves = &VectorParameterNameAndCurve;
			break;
		}
	}
	if ( ExistingCurves == nullptr )
	{
		int32 NewIndex = VectorParameterNamesAndCurves.Add( FVectorParameterNameAndCurves( InParameterName ) );
		ExistingCurves = &VectorParameterNamesAndCurves[NewIndex];

		ReconstructChannelProxy(true);
	}

	ExistingCurves->XCurve.AddCubicKey(InTime, InValue.X);
	ExistingCurves->YCurve.AddCubicKey(InTime, InValue.Y);
	ExistingCurves->ZCurve.AddCubicKey(InTime, InValue.Z);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}

void UMovieSceneParameterSection::AddColorParameterKey( FName InParameterName, FFrameNumber InTime, FLinearColor InValue )
{
	FColorParameterNameAndCurves* ExistingCurves = nullptr;
	for ( FColorParameterNameAndCurves& ColorParameterNameAndCurve : ColorParameterNamesAndCurves )
	{
		if ( ColorParameterNameAndCurve.ParameterName == InParameterName )
		{
			ExistingCurves = &ColorParameterNameAndCurve;
			break;
		}
	}
	if ( ExistingCurves == nullptr )
	{
		int32 NewIndex = ColorParameterNamesAndCurves.Add( FColorParameterNameAndCurves( InParameterName ) );
		ExistingCurves = &ColorParameterNamesAndCurves[NewIndex];

		ReconstructChannelProxy(true);
	}

	ExistingCurves->RedCurve.AddCubicKey(   InTime, InValue.R );
	ExistingCurves->GreenCurve.AddCubicKey( InTime, InValue.G );
	ExistingCurves->BlueCurve.AddCubicKey(  InTime, InValue.B );
	ExistingCurves->AlphaCurve.AddCubicKey( InTime, InValue.A );

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}

void UMovieSceneParameterSection::AddTransformParameterKey(FName InParameterName, FFrameNumber InTime, const FTransform& InValue)
{
	FTransformParameterNameAndCurves* ExistingCurves = nullptr;
	for (FTransformParameterNameAndCurves& TransformParameterNamesAndCurve : TransformParameterNamesAndCurves)
	{
		if (TransformParameterNamesAndCurve.ParameterName == InParameterName)
		{
			ExistingCurves = &TransformParameterNamesAndCurve;
			break;
		}
	}
	if (ExistingCurves == nullptr)
	{
		int32 NewIndex = TransformParameterNamesAndCurves.Add(FTransformParameterNameAndCurves(InParameterName));
		ExistingCurves = &TransformParameterNamesAndCurves[NewIndex];

		ReconstructChannelProxy(true);
	}
	FVector Translation = InValue.GetTranslation();
	FRotator Rotator = InValue.GetRotation().Rotator();
	FVector Scale = InValue.GetScale3D();
	ExistingCurves->Translation[0].AddCubicKey(InTime, Translation[0]);
	ExistingCurves->Translation[1].AddCubicKey(InTime, Translation[1]);
	ExistingCurves->Translation[2].AddCubicKey(InTime, Translation[2]);

	ExistingCurves->Rotation[0].AddCubicKey(InTime, Rotator.Roll);
	ExistingCurves->Rotation[1].AddCubicKey(InTime, Rotator.Pitch);
	ExistingCurves->Rotation[2].AddCubicKey(InTime, Rotator.Yaw);

	ExistingCurves->Scale[0].AddCubicKey(InTime, Scale[0]);
	ExistingCurves->Scale[1].AddCubicKey(InTime, Scale[1]);
	ExistingCurves->Scale[2].AddCubicKey(InTime, Scale[2]);

	if (TryModify())
	{
		SetRange(TRange<FFrameNumber>::Hull(TRange<FFrameNumber>(InTime), GetRange()));
	}
}

bool UMovieSceneParameterSection::RemoveScalarParameter( FName InParameterName )
{
	for ( int32 i = 0; i < ScalarParameterNamesAndCurves.Num(); i++ )
	{
		if ( ScalarParameterNamesAndCurves[i].ParameterName == InParameterName )
		{
			ScalarParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}

bool UMovieSceneParameterSection::RemoveBoolParameter(FName InParameterName)
{
	for (int32 i = 0; i < BoolParameterNamesAndCurves.Num(); i++)
	{
		if (BoolParameterNamesAndCurves[i].ParameterName == InParameterName)
		{
			BoolParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}

bool UMovieSceneParameterSection::RemoveVector2DParameter(FName InParameterName)
{
	for (int32 i = 0; i < Vector2DParameterNamesAndCurves.Num(); i++)
	{
		if (Vector2DParameterNamesAndCurves[i].ParameterName == InParameterName)
		{
			Vector2DParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}

bool UMovieSceneParameterSection::RemoveVectorParameter( FName InParameterName )
{
	for ( int32 i = 0; i < VectorParameterNamesAndCurves.Num(); i++ )
	{
		if ( VectorParameterNamesAndCurves[i].ParameterName == InParameterName )
		{
			VectorParameterNamesAndCurves.RemoveAt( i );
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}

bool UMovieSceneParameterSection::RemoveColorParameter( FName InParameterName )
{
	for ( int32 i = 0; i < ColorParameterNamesAndCurves.Num(); i++ )
	{
		if ( ColorParameterNamesAndCurves[i].ParameterName == InParameterName )
		{
			ColorParameterNamesAndCurves.RemoveAt( i );
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}

bool UMovieSceneParameterSection::RemoveTransformParameter(FName InParameterName)
{
	for (int32 i = 0; i < TransformParameterNamesAndCurves.Num(); i++)
	{
		if (TransformParameterNamesAndCurves[i].ParameterName == InParameterName)
		{
			TransformParameterNamesAndCurves.RemoveAt(i);
			ReconstructChannelProxy(true);
			return true;
		}
	}
	return false;
}

TArray<FScalarParameterNameAndCurve>& UMovieSceneParameterSection::GetScalarParameterNamesAndCurves()
{
	return ScalarParameterNamesAndCurves;
}

const TArray<FScalarParameterNameAndCurve>& UMovieSceneParameterSection::GetScalarParameterNamesAndCurves() const
{
	return ScalarParameterNamesAndCurves;
}

TArray<FBoolParameterNameAndCurve>& UMovieSceneParameterSection::GetBoolParameterNamesAndCurves() 
{
	return BoolParameterNamesAndCurves;
}

const TArray<FBoolParameterNameAndCurve>& UMovieSceneParameterSection::GetBoolParameterNamesAndCurves() const
{
	return BoolParameterNamesAndCurves;
}

TArray<FVector2DParameterNameAndCurves>& UMovieSceneParameterSection::GetVector2DParameterNamesAndCurves()
{
	return Vector2DParameterNamesAndCurves;
}

const TArray<FVector2DParameterNameAndCurves>& UMovieSceneParameterSection::GetVector2DParameterNamesAndCurves() const
{
	return Vector2DParameterNamesAndCurves;
}

TArray<FVectorParameterNameAndCurves>& UMovieSceneParameterSection::GetVectorParameterNamesAndCurves() 
{
	return VectorParameterNamesAndCurves;
}

const TArray<FVectorParameterNameAndCurves>& UMovieSceneParameterSection::GetVectorParameterNamesAndCurves() const
{
	return VectorParameterNamesAndCurves;
}

TArray<FColorParameterNameAndCurves>& UMovieSceneParameterSection::GetColorParameterNamesAndCurves()
{
	return ColorParameterNamesAndCurves;
}

const TArray<FColorParameterNameAndCurves>& UMovieSceneParameterSection::GetColorParameterNamesAndCurves() const
{
	return ColorParameterNamesAndCurves;
}

TArray<FTransformParameterNameAndCurves>& UMovieSceneParameterSection::GetTransformParameterNamesAndCurves() 
{
	return TransformParameterNamesAndCurves;
}

const TArray<FTransformParameterNameAndCurves>& UMovieSceneParameterSection::GetTransformParameterNamesAndCurves() const
{
	return TransformParameterNamesAndCurves;
}

void UMovieSceneParameterSection::GetParameterNames( TSet<FName>& ParameterNames ) const
{
	for ( const FScalarParameterNameAndCurve& ScalarParameterNameAndCurve : ScalarParameterNamesAndCurves )
	{
		ParameterNames.Add( ScalarParameterNameAndCurve.ParameterName );
	}
	for ( const FVectorParameterNameAndCurves& VectorParameterNameAndCurves : VectorParameterNamesAndCurves )
	{
		ParameterNames.Add( VectorParameterNameAndCurves.ParameterName );
	}
	for ( const FColorParameterNameAndCurves& ColorParameterNameAndCurves : ColorParameterNamesAndCurves )
	{
		ParameterNames.Add( ColorParameterNameAndCurves.ParameterName );
	}
	for (const FTransformParameterNameAndCurves& TransformParameterNamesAndCurve : TransformParameterNamesAndCurves)
	{
		ParameterNames.Add(TransformParameterNamesAndCurve.ParameterName);
	}
}