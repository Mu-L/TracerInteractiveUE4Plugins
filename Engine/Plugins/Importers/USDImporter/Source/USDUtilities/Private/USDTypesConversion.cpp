// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDTypesConversion.h"

#include "USDConversionUtils.h"

#include "Containers/StringConv.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/base/gf/vec2f.h"
	#include "pxr/base/gf/vec3f.h"
	#include "pxr/base/gf/vec4f.h"

	#include "pxr/usd/sdf/path.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/tokens.h"
#include "USDIncludesEnd.h"

FUsdStageInfo::FUsdStageInfo( const pxr::UsdStageRefPtr& Stage )
{
	pxr::TfToken UsdStageAxis = UsdUtils::GetUsdStageAxis( Stage );

	if ( UsdStageAxis == pxr::UsdGeomTokens->y )
	{
		UpAxis = EUsdUpAxis::YAxis;
	}
	else
	{
		UpAxis = EUsdUpAxis::ZAxis;
	}

	MetersPerUnit = UsdUtils::GetUsdStageMetersPerUnit( Stage );
}

namespace UsdTypesConversion
{
	FTransform ConvertAxes( const bool bZUp, const FTransform Transform )
	{
		FVector Translation = Transform.GetTranslation();
		FQuat Rotation = Transform.GetRotation();
		FVector Scale = Transform.GetScale3D();

		if ( bZUp )
		{
			Translation.Y = -Translation.Y;
			Rotation.X = -Rotation.X;
			Rotation.Z = -Rotation.Z;
		}
		else
		{
			Swap( Translation.Y, Translation.Z );

			Rotation = Rotation.Inverse();
			Swap( Rotation.Y, Rotation.Z );

			Swap( Scale.Y, Scale.Z );
		}

		return FTransform( Rotation, Translation, Scale );
	}
}

namespace UsdToUnreal
{
	FString ConvertString( const std::string& InString )
	{
		return FString( ANSI_TO_TCHAR( InString.c_str() ) );
	}

	FString ConvertString( std::string&& InString )
	{
		TUsdStore< std::string > UsdString( MoveTemp( InString ) ); // Store the temporary so that it gets destroyed with the USD allocator

		return FString( ANSI_TO_TCHAR( UsdString.Get().c_str() ) );
	}

	FString ConvertString( const char* InString )
	{
		return FString( ANSI_TO_TCHAR( InString ) );
	}

	FString ConvertPath( const pxr::SdfPath& Path )
	{
		return ConvertString( Path.GetString().c_str() );
	}

	FName ConvertName( const char* InString )
	{
		return FName( InString );
	}

	FName ConvertName( const std::string& InString )
	{
		return FName( InString.c_str() );
	}

	FName ConvertName( std::string&& InString )
	{
		TUsdStore< std::string > UsdString( MoveTemp( InString ) ); // Store the temporary so that it gets destroyed with the USD allocator

		return FName( UsdString.Get().c_str() );
	}

	FString ConvertToken( const pxr::TfToken& Token )
	{
		return UsdToUnreal::ConvertString( Token.GetString() );
	}

	FLinearColor ConvertColor( const pxr::GfVec3f& InValue )
	{
		return FLinearColor( InValue[0], InValue[1], InValue[2] );
	}

	FLinearColor ConvertColor( const pxr::GfVec4f& InValue )
	{
		return FLinearColor( InValue[0], InValue[1], InValue[2], InValue[3] );
	}

	FVector2D ConvertVector( const pxr::GfVec2f& InValue )
	{
		return FVector2D( InValue[0], InValue[1] );
	}

	FVector ConvertVector( const pxr::GfVec3f& InValue )
	{
		return FVector( InValue[0], InValue[1], InValue[2] );
	}

	FVector ConvertVector( const FUsdStageInfo& StageInfo, const pxr::GfVec3f& InValue )
	{
		FVector Value = ConvertVector( InValue );

		const float UEMetersPerUnit = 0.01f;
		if ( !FMath::IsNearlyEqual( StageInfo.MetersPerUnit, UEMetersPerUnit ) )
		{
			Value *= StageInfo.MetersPerUnit / UEMetersPerUnit;
		}

		const bool bIsZUp = ( StageInfo.UpAxis == EUsdUpAxis::ZAxis );

		if ( bIsZUp )
		{
			Value.Y = -Value.Y;
		}
		else
		{
			Swap( Value.Y, Value.Z );
		}

		return Value;
	}

	FMatrix ConvertMatrix( const pxr::GfMatrix4d& Matrix )
	{
		FMatrix UnrealMatrix(
			FPlane(Matrix[0][0], Matrix[0][1], Matrix[0][2], Matrix[0][3]),
			FPlane(Matrix[1][0], Matrix[1][1], Matrix[1][2], Matrix[1][3]),
			FPlane(Matrix[2][0], Matrix[2][1], Matrix[2][2], Matrix[2][3]),
			FPlane(Matrix[3][0], Matrix[3][1], Matrix[3][2], Matrix[3][3])
		);

		return UnrealMatrix;
	}

	FTransform ConvertMatrix( const FUsdStageInfo& StageInfo, const pxr::GfMatrix4d& InMatrix )
	{
		FMatrix Matrix = ConvertMatrix( InMatrix );
		FTransform Transform( Matrix );

		Transform = UsdTypesConversion::ConvertAxes( StageInfo.UpAxis == EUsdUpAxis::ZAxis, Transform );

		const float UEMetersPerUnit = 0.01f;
		if ( !FMath::IsNearlyEqual( StageInfo.MetersPerUnit, UEMetersPerUnit ) )
		{
			Transform.ScaleTranslation( StageInfo.MetersPerUnit / UEMetersPerUnit );
		}

		return Transform;
	}

	float ConvertDistance( const FUsdStageInfo& StageInfo, const float InValue )
	{
		float Value = InValue;

		const float UEMetersPerUnit = 0.01f;
		if ( !FMath::IsNearlyEqual( StageInfo.MetersPerUnit, UEMetersPerUnit ) )
		{
			Value *= StageInfo.MetersPerUnit / UEMetersPerUnit;
		}

		return Value;
	}
}

namespace UnrealToUsd
{
	TUsdStore< std::string > ConvertString( const TCHAR* InString )
	{
		return MakeUsdStore< std::string >( TCHAR_TO_ANSI( InString ) );
	}

	TUsdStore< pxr::SdfPath > ConvertPath( const TCHAR* InString )
	{
		return MakeUsdStore< pxr::SdfPath >( TCHAR_TO_ANSI( InString ) );
	}

	TUsdStore< std::string > ConvertName( const FName& InName )
	{
		return MakeUsdStore< std::string >( TCHAR_TO_ANSI( *InName.ToString() ) );
	}

	TUsdStore< pxr::TfToken > ConvertToken( const TCHAR* InString )
	{
		return MakeUsdStore< pxr::TfToken >( TCHAR_TO_ANSI( InString ) );
	}

	pxr::GfVec4f ConvertColor( const FLinearColor& InValue )
	{
		return pxr::GfVec4f( InValue.R, InValue.G, InValue.B, InValue.A );
	}

	pxr::GfVec4f ConvertColor( const FColor& InValue )
	{
		return ConvertColor( InValue.ReinterpretAsLinear() );
	}

	pxr::GfVec2f ConvertVector( const FVector2D& InValue )
	{
		return pxr::GfVec2f( InValue[0], InValue[1] );
	}

	pxr::GfVec3f ConvertVector( const FVector& InValue )
	{
		return pxr::GfVec3f( InValue[0], InValue[1], InValue[2] );
	}

	pxr::GfVec3f ConvertVector( const FUsdStageInfo& StageInfo, const FVector& InValue )
	{
		pxr::GfVec3f Value = ConvertVector( InValue );

		const float UEMetersPerUnit = 0.01f;
		if ( !FMath::IsNearlyEqual( StageInfo.MetersPerUnit, UEMetersPerUnit ) )
		{
			Value *= StageInfo.MetersPerUnit * UEMetersPerUnit;
		}

		const bool bIsZUp = ( StageInfo.UpAxis == EUsdUpAxis::ZAxis );

		if ( bIsZUp )
		{
			Value[1] = -Value[1];
		}
		else
		{
			Swap( Value[1], Value[2] );
		}

		return Value;
	}

	pxr::GfMatrix4d ConvertMatrix( const FMatrix& Matrix )
	{
		pxr::GfMatrix4d UsdMatrix(
			Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2], Matrix.M[0][3],
			Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2], Matrix.M[1][3],
			Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2], Matrix.M[2][3],
			Matrix.M[3][0], Matrix.M[3][1], Matrix.M[3][2], Matrix.M[3][3]
		);

		return UsdMatrix;
	}

	pxr::GfMatrix4d ConvertTransform( const FUsdStageInfo& StageInfo, const FTransform& Transform )
	{
		FTransform TransformInUsdSpace = UsdTypesConversion::ConvertAxes( StageInfo.UpAxis == EUsdUpAxis::ZAxis, Transform );

		const float UEMetersPerUnit = 0.01f;
		if ( !FMath::IsNearlyEqual( StageInfo.MetersPerUnit, UEMetersPerUnit ) )
		{
			TransformInUsdSpace.ScaleTranslation( StageInfo.MetersPerUnit * UEMetersPerUnit );
		}

		return ConvertMatrix( TransformInUsdSpace.ToMatrixWithScale() );
	}

	float ConvertDistance( const FUsdStageInfo& StageInfo, const float& InValue )
	{
		float Value = InValue;

		const float UEMetersPerUnit = 0.01f;
		if ( !FMath::IsNearlyEqual( StageInfo.MetersPerUnit, UEMetersPerUnit ) )
		{
			Value *= StageInfo.MetersPerUnit * UEMetersPerUnit;
		}

		return Value;
	}
}

#endif // #if USE_USD_SDK
