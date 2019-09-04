// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Vector4.h"
#endif

#include "Chaos/Array.h"
#include "Chaos/Defines.h"
#include "Chaos/Pair.h"

#include "Containers/StaticArray.h"
#include <iostream>
#include <utility>

namespace Chaos
{
	template<class T, int d>
	class TVector : public TArray<T>
	{
	public:
		using TArray<T>::SetNum;

		TVector()
		    : TArray<T>()
		{
			SetNum(d);
		}
		explicit TVector(const T element)
		    : TArray<T>()
		{
			SetNum(d);
			for (int32 i = 0; i < d; ++i)
				(*this)[i] = element;
		}
		TVector(const T s1, const T s2)
		{
			check(d == 2);
			SetNum(2);
			(*this)[0] = s1;
			(*this)[1] = s2;
		}
		TVector(const T s1, const T s2, const T s3)
		{
			check(d == 3);
			SetNum(3);
			(*this)[0] = s1;
			(*this)[1] = s2;
			(*this)[2] = s3;
		}
		TVector(const T s1, const T s2, const T s3, const T s4)
		{
			check(d == 4);
			SetNum(4);
			(*this)[0] = s1;
			(*this)[1] = s2;
			(*this)[2] = s3;
			(*this)[3] = s4;
		}
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
		TVector(const FVector& Other)
		{
			check(d == 3);
			SetNum(3);
			(*this)[0] = static_cast<T>(Other.X);
			(*this)[1] = static_cast<T>(Other.Y);
			(*this)[2] = static_cast<T>(Other.Z);
		}
#endif
		template<class T2>
		TVector(const TVector<T2, d>& Other)
		{
			SetNum(d);
			for (int32 i = 0; i < d; ++i)
			{
				(*this)[i] = static_cast<T>(Other[i]);
			}
		}
		TVector(std::istream& Stream)
		{
			for (int32 i = 0; i < d; ++i)
			{
				Stream.read(reinterpret_cast<char*>(&(*this)[i]), sizeof(T));
			}
		}
		~TVector() {}
		void Write(std::ostream& Stream) const
		{
			for (int32 i = 0; i < d; ++i)
			{
				Stream.write(reinterpret_cast<const char*>(&(*this)[i]), sizeof(T));
			}
		}
		TVector<T, d>& operator=(const TVector<T, d>& Other)
		{
			for (int32 i = 0; i < d; ++i)
			{
				(*this)[i] = Other[i];
			}
			return *this;
		}
		T Size() const
		{
			T SquaredSum = 0;
			for (int32 i = 0; i < d; ++i)
			{
				SquaredSum += ((*this)[i] * (*this)[i]);
			}
			return sqrt(SquaredSum);
		}
		T Product() const
		{
			T Result = 1;
			for (int32 i = 0; i < d; ++i)
			{
				Result *= (*this)[i];
			}
			return Result;
		}
		static TVector<T, d> AxisVector(const int32 Axis)
		{
			check(Axis >= 0 && Axis < d);
			TVector<T, d> Result(0);
			Result[Axis] = (T)1;
			return Result;
		}
		T SizeSquared() const
		{
			T Result = 0;
			for (int32 i = 0; i < d; ++i)
			{
				Result += ((*this)[i] * (*this)[i]);
			}
			return Result;
		}
		TVector<T, d> GetSafeNormal() const
		{
			//We want N / ||N|| and to avoid inf
			//So we want N / ||N|| < 1 / eps => N eps < ||N||, but this is clearly true for all eps < 1 and N > 0
			T SizeSqr = SizeSquared();
			if (SizeSqr <= TNumericLimits<T>::Min())
				return AxisVector(0);
			return (*this) / sqrt(SizeSqr);
		}
		T SafeNormalize()
		{
			T Size = SizeSquared();
			if (Size < (T)1e-4)
			{
				*this = AxisVector(0);
				return (T)0.;
			}
			Size = sqrt(Size);
			*this = (*this) / Size;
			return Size;
		}
		TVector<T, d> operator-() const
		{
			TVector<T, d> Result;
			for (int32 i = 0; i < d; ++i)
			{
				Result[i] = -(*this)[i];
			}
			return Result;
		}
		TVector<T, d> operator*(const TVector<T, d>& Other) const
		{
			TVector<T, d> Result;
			for (int32 i = 0; i < d; ++i)
			{
				Result[i] = (*this)[i] * Other[i];
			}
			return Result;
		}
		TVector<T, d> operator/(const TVector<T, d>& Other) const
		{
			TVector<T, d> Result;
			for (int32 i = 0; i < d; ++i)
			{
				Result[i] = (*this)[i] / Other[i];
			}
			return Result;
		}
		TVector<T, d> operator+(const TVector<T, d>& Other) const
		{
			TVector<T, d> Result;
			for (int32 i = 0; i < d; ++i)
			{
				Result[i] = (*this)[i] + Other[i];
			}
			return Result;
		}
		TVector<T, d> operator-(const TVector<T, d>& Other) const
		{
			TVector<T, d> Result;
			for (int32 i = 0; i < d; ++i)
			{
				Result[i] = (*this)[i] - Other[i];
			}
			return Result;
		}
		TVector<T, d>& operator+=(const TVector<T, d>& Other)
		{
			for (int32 i = 0; i < d; ++i)
			{
				(*this)[i] += Other[i];
			}
			return *this;
		}
		TVector<T, d>& operator-=(const TVector<T, d>& Other)
		{
			for (int32 i = 0; i < d; ++i)
			{
				(*this)[i] -= Other[i];
			}
			return *this;
		}
		TVector<T, d>& operator/=(const TVector<T, d>& Other)
		{
			for (int32 i = 0; i < d; ++i)
			{
				(*this)[i] /= Other[i];
			}
			return *this;
		}
		TVector<T, d> operator*(const T& S) const
		{
			TVector<T, d> Result;
			for (int32 i = 0; i < d; ++i)
			{
				Result[i] = (*this)[i] * S;
			}
			return Result;
		}
		TVector<T, d>& operator*=(const T& S)
		{
			for (int32 i = 0; i < d; ++i)
			{
				(*this)[i] *= S;
			}
			return *this;
		}
#if COMPILE_WITHOUT_UNREAL_SUPPORT
		static inline float DotProduct(const Vector<float, 3>& V1, const Vector<float, 3>& V2)
		{
			return V1[0] * V2[0] + V1[1] * V2[1] + V1[2] * V2[2];
		}
		static inline Vector<float, 3> CrossProduct(const Vector<float, 3>& V1, const Vector<float, 3>& V2)
		{
			Vector<float, 3> Result;
			Result[0] = V1[1] * V2[2] - V1[2] * V2[1];
			Result[1] = V1[2] * V2[0] - V1[0] * V2[2];
			Result[2] = V1[0] * V2[1] - V1[1] * V2[0];
			return Result;
		}
#endif
	};
	template<class T, int d>
	inline TVector<T, d> operator*(const T S, const TVector<T, d>& V)
	{
		TVector<T, d> Ret;
		for (int32 i = 0; i < d; ++i)
		{
			Ret[i] = S * V[i];
		}
		return Ret;
	}
	template<class T, int d>
	inline TVector<T, d> operator/(const T S, const TVector<T, d>& V)
	{
		TVector<T, d> Ret;
		for (int32 i = 0; i < d; ++i)
		{
			Ret = S / V[i];
		}
		return Ret;
	}

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
	template<>
	class TVector<float, 4> : public FVector4
	{
	public:
		using FVector4::W;
		using FVector4::X;
		using FVector4::Y;
		using FVector4::Z;

		TVector()
		    : FVector4() {}
		explicit TVector(const float x)
		    : FVector4(x, x, x, x) {}
		TVector(const float x, const float y, const float z, const float w)
		    : FVector4(x, y, z, w) {}
		TVector(const FVector4& vec)
		    : FVector4(vec) {}
	};

	template<>
	class TVector<float, 3> : public FVector
	{
	public:
		using FVector::X;
		using FVector::Y;
		using FVector::Z;

		TVector()
		    : FVector() {}
		explicit TVector(const float x)
		    : FVector(x, x, x) {}
		TVector(const float x, const float y, const float z)
		    : FVector(x, y, z) {}
		TVector(const FVector& vec)
		    : FVector(vec) {}
		TVector(const FVector4& vec)
		    : FVector(vec.X, vec.Y, vec.Z) {}
		TVector(std::istream& Stream)
		{
			Stream.read(reinterpret_cast<char*>(&X), sizeof(X));
			Stream.read(reinterpret_cast<char*>(&Y), sizeof(Y));
			Stream.read(reinterpret_cast<char*>(&Z), sizeof(Z));
		}
		~TVector() {}
		void Write(std::ostream& Stream) const
		{
			Stream.write(reinterpret_cast<const char*>(&X), sizeof(X));
			Stream.write(reinterpret_cast<const char*>(&Y), sizeof(Y));
			Stream.write(reinterpret_cast<const char*>(&Z), sizeof(Z));
		}
		static inline TVector<float, 3> CrossProduct(const TVector<float, 3>& V1, const TVector<float, 3>& V2) { return FVector::CrossProduct(V1, V2); }
		static inline float DotProduct(const TVector<float, 3>& V1, const TVector<float, 3>& V2) { return FVector::DotProduct(V1, V2); }
		bool operator<=(const TVector<float, 3>& V) const
		{
			return X <= V.X && Y <= V.Y && Z <= V.Z;
		}
		bool operator>=(const TVector<float, 3>& V) const
		{
			return X >= V.X && Y >= V.Y && Z >= V.Z;
		}
		TVector<float, 3> operator-() const
		{
			return TVector<float, 3>(-X, -Y, -Z);
		}
		TVector<float, 3> operator-(const float Other) const
		{
			return TVector<float, 3>(X - Other, Y - Other, Z - Other);
		}
		TVector<float, 3> operator*(const float Other) const
		{
			return TVector<float, 3>(X * Other, Y * Other, Z * Other);
		}
		TVector<float, 3> operator/(const float Other) const
		{
			return TVector<float, 3>(X / Other, Y / Other, Z / Other);
		}
		template<class T2>
		TVector<float, 3> operator-(const TVector<T2, 3>& Other) const
		{
			return TVector<float, 3>(X - Other[0], Y - Other[1], Z - Other[2]);
		}
		template<class T2>
		TVector<float, 3> operator*(const TVector<T2, 3>& Other) const
		{
			return TVector<float, 3>(X * Other[0], Y * Other[1], Z * Other[2]);
		}
		template<class T2>
		TVector<float, 3> operator/(const TVector<T2, 3>& Other) const
		{
			return TVector<float, 3>(X / Other[0], Y / Other[1], Z / Other[2]);
		}
		float Product() const
		{
			return X * Y * Z;
		}
		float Max() const
		{
			return (X > Y && X > Z) ? X : (Y > Z ? Y : Z);
		}
		float Min() const
		{
			return (X < Y && X < Z) ? X : (Y < Z ? Y : Z);
		}
		TVector<float, 3> ComponentwiseMin(const TVector<float, 3>& Other) const { return {FMath::Min(X,Other.X), FMath::Min(Y,Other.Y), FMath::Min(Z,Other.Z)}; }
		TVector<float, 3> ComponentwiseMax(const TVector<float, 3>& Other) const { return {FMath::Max(X,Other.X), FMath::Max(Y,Other.Y), FMath::Max(Z,Other.Z)}; }
		static TVector<float, 3> Max(const TVector<float, 3>& V1, const TVector<float, 3>& V2)
		{
			return TVector<float, 3>(V1.X > V2.X ? V1.X : V2.X, V1.Y > V2.Y ? V1.Y : V2.Y, V1.Z > V2.Z ? V1.Z : V2.Z);
		}
		static TVector<float, 3> AxisVector(const int32 Axis)
		{ return Axis == 0 ? TVector<float, 3>(1.f, 0.f, 0.f) : (Axis == 1 ? TVector<float, 3>(0.f, 1.f, 0.f) : TVector<float, 3>(0.f, 0.f, 1.f)); }
		static Pair<float, int32> MaxAndAxis(const TVector<float, 3>& V1, const TVector<float, 3>& V2)
		{
			const TVector<float, 3> max = Max(V1, V2);
			if (max.X > max.Y)
			{
				if (max.X > max.Z)
					return MakePair(max.X, 0);
				else
					return MakePair(max.Z, 2);
			}
			else
			{
				if (max.Y > max.Z)
					return MakePair(max.Y, 1);
				else
					return MakePair(max.Z, 2);
			}
		}
		float SafeNormalize()
		{
			float Size = SizeSquared();
			if (Size < (float)1e-4)
			{
				*this = AxisVector(0);
				return 0.f;
			}
			Size = sqrt(Size);
			*this = (*this) / Size;
			return Size;
		}
		TVector<float, 3> GetOrthogonalVector() const
		{
			TVector<float, 3> AbsVector(FMath::Abs(X), FMath::Abs(Y), FMath::Abs(Z));
			if ((AbsVector.X <= AbsVector.Y) && (AbsVector.X <= AbsVector.Z))
			{
				// X is the smallest component
				return TVector<float, 3>(0, Z, -Y);
			}
			if ((AbsVector.Z <= AbsVector.X) && (AbsVector.Z <= AbsVector.Y))
			{
				// Z is the smallest component
				return TVector<float, 3>(Y, -X, 0);
			}
			// Y is the smallest component
			return TVector<float, 3>(-Z, 0, X);
		}
		static float AngleBetween(const TVector<float, 3>& V1, const TVector<float, 3>& V2)
		{
			float s = CrossProduct(V1, V2).Size();
			float c = DotProduct(V1, V2);
			return atan2(s, c);
		}
	};
	template<>
	inline TVector<float, 3> operator/(const float S, const TVector<float, 3>& V)
	{
		return TVector<float, 3>(S / V.X, S / V.Y, S / V.Z);
	}

	template<>
	class TVector<float, 2> : public FVector2D
	{
	public:
		TVector()
		    : FVector2D() {}
		TVector(const float x)
		    : FVector2D(x, x) {}
		TVector(const float x, const float y)
		    : FVector2D(x, y) {}
		TVector(const FVector2D& vec)
		    : FVector2D(vec) {}
		TVector(std::istream& Stream)
		{
			Stream.read(reinterpret_cast<char*>(&X), sizeof(X));
			Stream.read(reinterpret_cast<char*>(&Y), sizeof(Y));
		}
		~TVector() {}
		void Write(std::ostream& Stream) const
		{
			Stream.write(reinterpret_cast<const char*>(&X), sizeof(X));
			Stream.write(reinterpret_cast<const char*>(&Y), sizeof(Y));
		}

		static TVector<float, 2> AxisVector(const int32 Axis)
		{
			check(Axis >= 0 && Axis <= 1);
			return Axis == 0 ? TVector<float, 2>(1.f, 0.f) : TVector<float, 2>(0.f, 1.f);
		}
		float Product() const
		{
			return X * Y;
		}
		float Max() const
		{
			return X > Y ? X : Y;
		}
		float Min() const
		{
			return X < Y ? X : Y;
		}
		static TVector<float, 2> Max(const TVector<float, 2>& V1, const TVector<float, 2>& V2)
		{
			return TVector<float, 2>(V1.X > V2.X ? V1.X : V2.X, V1.Y > V2.Y ? V1.Y : V2.Y);
		}
		static Pair<float, int32> MaxAndAxis(const TVector<float, 2>& V1, const TVector<float, 2>& V2)
		{
			const TVector<float, 2> max = Max(V1, V2);
			if (max.X > max.Y)
			{
				return MakePair(max.X, 0);
			}
			else
			{
				return MakePair(max.Y, 1);
			}
		}
	};
#endif // !COMPILE_WITHOUT_UNREAL_SUPPORT

	template<class T>
	class TVector<T, 3>
	{
	public:
		FORCEINLINE TVector() {}
		FORCEINLINE explicit TVector(T InX)
		    : X(InX), Y(InX), Z(InX) {}
		FORCEINLINE TVector(T InX, T InY, T InZ)
		    : X(InX), Y(InY), Z(InZ) {}

		FORCEINLINE int32 Num() const { return 3; }
		FORCEINLINE bool operator==(const TVector<T, 3>& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
		FORCEINLINE TVector(const FVector& Other)
		{
			X = static_cast<T>(Other.X);
			Y = static_cast<T>(Other.Y);
			Z = static_cast<T>(Other.Z);
		}
#endif
		template<class T2>
		FORCEINLINE TVector(const TVector<T2, 3>& Other)
		    : X(static_cast<T>(Other.X))
		    , Y(static_cast<T>(Other.Y))
		    , Z(static_cast<T>(Other.Z))
		{}

		FORCEINLINE TVector(std::istream& Stream)
		{
			Stream.read(reinterpret_cast<char*>(&X), sizeof(T));
			Stream.read(reinterpret_cast<char*>(&Y), sizeof(T));
			Stream.read(reinterpret_cast<char*>(&Z), sizeof(T));
		}
		FORCEINLINE ~TVector() {}
		FORCEINLINE void Write(std::ostream& Stream) const
		{
			Stream.write(reinterpret_cast<const char*>(&X), sizeof(T));
			Stream.write(reinterpret_cast<const char*>(&Y), sizeof(T));
			Stream.write(reinterpret_cast<const char*>(&Z), sizeof(T));
		}
		FORCEINLINE TVector<T, 3>& operator=(const TVector<T, 3>& Other)
		{
			X = Other.X;
			Y = Other.Y;
			Z = Other.Z;
			return *this;
		}
		FORCEINLINE T Size() const
		{
			const T SquaredSum = X * X + Y * Y + Z * Z;
			return sqrt(SquaredSum);
		}
		FORCEINLINE T Product() const { return X * Y * Z; }
		FORCEINLINE static TVector<T, 3> AxisVector(const int32 Axis)
		{
			TVector<T, 3> Result(0);
			Result[Axis] = (T)1;
			return Result;
		}
		FORCEINLINE T SizeSquared() const { return X * X + Y * Y + Z * Z; }

		FORCEINLINE T Min() const { return FMath::Min3(X, Y, Z); }
		FORCEINLINE T Max() const { return FMath::Max3(X, Y, Z); }

		FORCEINLINE TVector<T, 3> ComponentwiseMin(const TVector<T, 3>& Other) const { return {FMath::Min(X,Other.X), FMath::Min(Y,Other.Y), FMath::Min(Z,Other.Z)}; }
		FORCEINLINE TVector<T, 3> ComponentwiseMax(const TVector<T, 3>& Other) const { return {FMath::Max(X,Other.X), FMath::Max(Y,Other.Y), FMath::Max(Z,Other.Z)}; }

		TVector<T, 3> GetSafeNormal() const
		{
			//We want N / ||N|| and to avoid inf
			//So we want N / ||N|| < 1 / eps => N eps < ||N||, but this is clearly true for all eps < 1 and N > 0
			T SizeSqr = SizeSquared();
			if (SizeSqr <= TNumericLimits<T>::Min())
				return AxisVector(0);
			return (*this) / sqrt(SizeSqr);
		}
		FORCEINLINE T SafeNormalize()
		{
			T Size = SizeSquared();
			if (Size < (T)1e-4)
			{
				*this = AxisVector(0);
				return (T)0.;
			}
			Size = sqrt(Size);
			*this = (*this) / Size;
			return Size;
		}

		FORCEINLINE T operator[](int32 Idx) const { return (static_cast<const T*>(&X))[Idx]; }
		FORCEINLINE T& operator[](int32 Idx) { return (static_cast<T*>(&X))[Idx]; }

		FORCEINLINE TVector<T, 3> operator-() const { return {-X, -Y, -Z}; }
		FORCEINLINE TVector<T, 3> operator*(const TVector<T, 3>& Other) const { return {X * Other.X, Y * Other.Y, Z * Other.Z}; }
		FORCEINLINE TVector<T, 3> operator/(const TVector<T, 3>& Other) const { return {X / Other.X, Y / Other.Y, Z / Other.Z}; }
		FORCEINLINE TVector<T, 3> operator/(const T Scalar) const { return {X / Scalar, Y / Scalar, Z / Scalar}; }
		FORCEINLINE TVector<T, 3> operator+(const TVector<T, 3>& Other) const { return {X + Other.X, Y + Other.Y, Z + Other.Z}; }
		FORCEINLINE TVector<T, 3> operator+(const T Scalar) const { return {X + Scalar, Y + Scalar, Z + Scalar}; }
		FORCEINLINE TVector<T, 3> operator-(const TVector<T, 3>& Other) const { return {X - Other.X, Y - Other.Y, Z - Other.Z}; }
		FORCEINLINE TVector<T, 3> operator-(const T Scalar) const { return {X - Scalar, Y - Scalar, Z - Scalar}; }

		FORCEINLINE TVector<T, 3>& operator+=(const TVector<T, 3>& Other)
		{
			X += Other.X;
			Y += Other.Y;
			Z += Other.Z;
			return *this;
		}
		FORCEINLINE TVector<T, 3>& operator-=(const TVector<T, 3>& Other)
		{
			X -= Other.X;
			Y -= Other.Y;
			Z -= Other.Z;
			return *this;
		}
		FORCEINLINE TVector<T, 3>& operator/=(const TVector<T, 3>& Other)
		{
			X /= Other.X;
			Y /= Other.Y;
			Z /= Other.Z;
			return *this;
		}
		FORCEINLINE TVector<T, 3> operator*(const T S) const { return {X * S, Y * S, Z * S}; }
		FORCEINLINE TVector<T, 3>& operator*=(const T S)
		{
			X *= S;
			Y *= S;
			Z *= S;
			return *this;
		}
#if COMPILE_WITHOUT_UNREAL_SUPPORT
		FORCEINLINE static inline float DotProduct(const Vector<float, 3>& V1, const Vector<float, 3>& V2)
		{
			return V1[0] * V2[0] + V1[1] * V2[1] + V1[2] * V2[2];
		}
		FORCEINLINE static inline Vector<float, 3> CrossProduct(const Vector<float, 3>& V1, const Vector<float, 3>& V2)
		{
			Vector<float, 3> Result;
			Result[0] = V1[1] * V2[2] - V1[2] * V2[1];
			Result[1] = V1[2] * V2[0] - V1[0] * V2[2];
			Result[2] = V1[0] * V2[1] - V1[1] * V2[0];
			return Result;
		}
#endif

		T X;
		T Y;
		T Z;
	};
	template<class T>
	inline TVector<T, 3> operator*(const T S, const TVector<T, 3>& V)
	{
		return TVector<T, 3>{V.X * S, V.Y * S, V.Z * S};
	}
	template<class T>
	inline TVector<T, 3> operator/(const T S, const TVector<T, 3>& V)
	{
		return TVector<T, 3>{V.X / S, V.Y / S, V.Z / S};
	}

	template<>
	class TVector<int32, 2>
	{
	public:
		FORCEINLINE TVector()
		{}
		FORCEINLINE explicit TVector(const int32 InX)
		    : X(InX), Y(InX)
		{}
		FORCEINLINE TVector(const int32 InX, const int32 InY)
		    : X(InX), Y(InY)
		{}
		FORCEINLINE ~TVector()
		{}

		FORCEINLINE int32 Num() const { return 2; }

		FORCEINLINE int32 Product() const
		{
			return X * Y;
		}

		FORCEINLINE static TVector<int32, 2>
		AxisVector(const int32 Axis)
		{
			TVector<int32, 2> Result(0);
			Result[Axis] = 1;
			return Result;
		}

		FORCEINLINE void Write(std::ostream& Stream) const
		{
			Stream.write(reinterpret_cast<const char*>(&X), sizeof(int32));
			Stream.write(reinterpret_cast<const char*>(&Y), sizeof(int32));
		}

		FORCEINLINE TVector<int32, 2>&
		operator=(const TVector<int32, 2>& Other)
		{
			X = Other.X;
			Y = Other.Y;
			return *this;
		}

		FORCEINLINE bool
		operator==(const TVector<int32, 2>& Other) const
		{
			return X == Other.X && Y == Other.Y;
		}

		FORCEINLINE int32 operator[](const int32 Idx) const { return (static_cast<const int32*>(&X))[Idx]; }
		FORCEINLINE int32& operator[](const int32 Idx) { return (static_cast<int32*>(&X))[Idx]; }

		FORCEINLINE TVector<int32, 2> operator-() const { return {-X, -Y}; }
		FORCEINLINE TVector<int32, 2> operator*(const TVector<int32, 2>& Other) const { return {X * Other.X, Y * Other.Y}; }
		FORCEINLINE TVector<int32, 2> operator/(const TVector<int32, 2>& Other) const { return {X / Other.X, Y / Other.Y}; }
		FORCEINLINE TVector<int32, 2> operator+(const TVector<int32, 2>& Other) const { return {X + Other.X, Y + Other.Y}; }
		FORCEINLINE TVector<int32, 2> operator-(const TVector<int32, 2>& Other) const { return {X - Other.X, Y - Other.Y}; }

		FORCEINLINE TVector<int32, 2>& operator+=(const TVector<int32, 2>& Other)
		{
			X += Other.X;
			Y += Other.Y;
			return *this;
		}
		FORCEINLINE TVector<int32, 2>& operator-=(const TVector<int32, 2>& Other)
		{
			X -= Other.X;
			Y -= Other.Y;
			return *this;
		}
		FORCEINLINE TVector<int32, 2>& operator/=(const TVector<int32, 2>& Other)
		{
			X /= Other.X;
			Y /= Other.Y;
			return *this;
		}
		FORCEINLINE TVector<int32, 2> operator*(const int32& S) const
		{
			return {X * S, Y * S};
		}
		FORCEINLINE TVector<int32, 2>& operator*=(const int32& S)
		{
			X *= S;
			Y *= S;
			return *this;
		}

	private:
		int32 X;
		int32 Y;
	};

	template<class T>
	inline uint32 GetTypeHash(const Chaos::TVector<T, 2>& V)
	{
		uint32 Seed = ::GetTypeHash(V[0]);
		Seed ^= ::GetTypeHash(V[1]) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
		return Seed;
	}

	template<class T>
	inline uint32 GetTypeHash(const Chaos::TVector<T, 3>& V)
	{
		uint32 Seed = ::GetTypeHash(V[0]);
		Seed ^= ::GetTypeHash(V[1]) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
		Seed ^= ::GetTypeHash(V[2]) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
		return Seed;
	}

	template<typename T, int d>
	FArchive& operator<<(FArchive& Ar, TVector<T, d>& ValueIn)
	{
		for (int32 Idx = 0; Idx < d; ++Idx)
		{
			Ar << ValueIn[Idx];
		}
		return Ar;
	}

} // namespace Chaos

//template<>
//uint32 GetTypeHash(const Chaos::TVector<int32, 2>& V)
//{
//	uint32 Seed = GetTypeHash(V[0]);
//	Seed ^= GetTypeHash(V[1]) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
//	return Seed;
//}
