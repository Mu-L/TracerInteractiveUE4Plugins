// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <limits>

namespace Electra
{
class FTimeFraction;

/**
 * Keeps a time value in hundred nanoseconds (HNS).
 */
class FTimeValue
{
public:
	static FTimeValue GetInvalid()
	{
		static FTimeValue kInvalid;
		return kInvalid;
	}

	static FTimeValue GetZero()
	{
		static FTimeValue kZero((int64)0);
		return kZero;
	}

	static FTimeValue GetPositiveInfinity()
	{
		static FTimeValue kPosInf(std::numeric_limits<double>::infinity());
		return kPosInf;
	}

	FTimeValue()
		: HNS(0)
		, bIsValid(false)
		, bIsInfinity(false)
	{
	}

	FTimeValue(const FTimeValue& rhs)
		: HNS(rhs.HNS)
		, bIsValid(rhs.bIsValid)
		, bIsInfinity(rhs.bIsInfinity)
	{
	}

	FTimeValue& operator=(const FTimeValue& rhs)
	{
		HNS 		= rhs.HNS;
		bIsValid	= rhs.bIsValid;
		bIsInfinity = rhs.bIsInfinity;
		return *this;
	}

	bool IsValid() const
	{
		return bIsValid;
	}

	bool IsInfinity() const
	{
		return bIsInfinity;
	}

	double GetAsSeconds(double DefaultIfInvalid=0.0) const
	{
		return bIsValid ? (bIsInfinity ? (HNS >= 0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity()) : HNS / 10000000.0) : DefaultIfInvalid;
	}

	int64 GetAsMilliseconds(int64 DefaultIfInvalid=0) const
	{
		return bIsValid ? (bIsInfinity ? (HNS >= 0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL) : HNS / 10000) : DefaultIfInvalid;
	}

	int64 GetAsMicroseconds(int64 DefaultIfInvalid=0) const
	{
		return bIsValid ? (bIsInfinity ? (HNS >= 0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL) : HNS / 10) : DefaultIfInvalid;
	}

	int64 GetAsHNS(int64 DefaultIfInvalid=0) const
	{
		return bIsValid ? (bIsInfinity ? (HNS >= 0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL) : HNS) : DefaultIfInvalid;
	}

	int64 GetAs90kHz(int64 DefaultIfInvalid=0) const
	{
		return bIsValid ? (bIsInfinity ? (HNS >= 0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL) : HNS * 9 / 1000) : DefaultIfInvalid;
	}

	//! Returns this time value in a custom timebase. Requires internal bigint conversion and is therefor SLOW!
	int64 GetAsTimebase(uint32 CustomTimebase) const;

	FTimeValue& SetToInvalid()
	{
		HNS 		= 0;
		bIsValid	= false;
		bIsInfinity = false;
		return *this;
	}

	FTimeValue& SetToZero()
	{
		HNS 		= 0;
		bIsValid	= true;
		bIsInfinity = false;
		return *this;
	}

	FTimeValue& SetToPositiveInfinity()
	{
		HNS 		= 0x7fffffffffffffffLL;
		bIsValid	= true;
		bIsInfinity = true;
		return *this;
	}

	FTimeValue& SetFromSeconds(double Seconds)
	{
		if ((bIsInfinity = (Seconds == std::numeric_limits<double>::infinity())) == true)
		{
			HNS = 0x7fffffffffffffffLL;
			bIsValid = true;
		}
		else
		{
			if ((bIsValid = Seconds >= -922337203685.0 && Seconds <= 922337203685.0) == true)
			{
				HNS = (int64)(Seconds * 10000000.0);
			}
			else
			{
				check(!"Value cannot be represented!");
				HNS = 0;
			}
		}
		return *this;
	}


	FTimeValue& SetFromMilliseconds(int64 Milliseconds)
	{
		bIsInfinity = false;
		if ((bIsValid = Milliseconds >= -922337203685477 && Milliseconds <= 922337203685477) == true)
		{
			HNS = Milliseconds * 10000;
		}
		else
		{
			check(!"Value cannot be represented!");
			HNS = 0;
		}
		return *this;
	}

	FTimeValue& SetFromMicroseconds(int64 Microseconds)
	{
		bIsInfinity = false;
		if ((bIsValid = Microseconds >= -922337203685477580 && Microseconds <= 922337203685477580) == true)
		{
			HNS = Microseconds * 10;
		}
		else
		{
			check(!"Value cannot be represented!");
			HNS = 0;
		}
		return *this;
	}

	FTimeValue& SetFromHNS(int64 InHNS)
	{
		HNS 		= InHNS;
		bIsValid	= true;
		bIsInfinity = false;
		return *this;
	}

	FTimeValue& SetFrom90kHz(int64 Ticks)
	{
		HNS 		= Ticks * 1000 / 9;
		bIsValid	= true;
		bIsInfinity = false;
		return *this;
	}

	FTimeValue& SetFromND(int64 Numerator, uint32 Denominator)
	{
		if (Denominator != 0)
		{
			if (Denominator == 10000000)
			{
				HNS 		= Numerator;
				bIsValid	= true;
				bIsInfinity = false;
			}
			else if (Numerator >= -922337203685LL && Numerator <= 922337203685LL)
			{
				HNS 		= Numerator * 10000000 / Denominator;
				bIsValid	= true;
				bIsInfinity = false;
			}
			else
			{
				// Need a bigint here!
				check(!"TODO");
				HNS 		= (int64)(Numerator * (10000000.0 / (double)Denominator));
				bIsValid	= true;
				bIsInfinity = false;
			}
		}
		else
		{
			HNS 		= Numerator>=0 ? 0x7fffffffffffffffLL : -0x7fffffffffffffffLL;
			bIsValid	= true;
			bIsInfinity = true;
		}
		return *this;
	}


	FTimeValue& SetFromTimeFraction(const FTimeFraction& TimeFraction);


	FTimeValue& AdvanceBySeconds(double Seconds)
	{
		FTimeValue tv;
		tv.SetFromSeconds(Seconds);
		return operator += (tv);
	}


	bool operator == (const FTimeValue& rhs) const
	{
		return (!bIsValid && !rhs.bIsValid) || (bIsValid == rhs.bIsValid && bIsInfinity == rhs.bIsInfinity && HNS == rhs.HNS);
	}

	bool operator != (const FTimeValue& rhs) const
	{
		return !(*this == rhs);
	}

	bool operator < (const FTimeValue& rhs) const
	{
		if (bIsValid && rhs.bIsValid)
		{
			if (!bIsInfinity)
			{
				return !rhs.bIsInfinity ? HNS < rhs.HNS : rhs.HNS > 0;
			}
			else
			{
				return rhs.bIsInfinity ? HNS < rhs.HNS : HNS < 0;
			}
		}
		return false;
	}

	bool operator <= (const FTimeValue& rhs) const
	{
		return (!bIsValid || !rhs.bIsValid) ? false : (*this < rhs || *this == rhs);
	}

	bool operator > (const FTimeValue &rhs) const
	{
		return (!bIsValid || !rhs.bIsValid) ? false : !(*this <= rhs);
	}

	bool operator >= (const FTimeValue &rhs) const
	{
		return (!bIsValid || !rhs.bIsValid) ? false : !(*this < rhs);
	}

	inline FTimeValue& operator += (const FTimeValue& rhs)
	{
		if (bIsValid)
		{
			if (rhs.bIsValid)
			{
				if (!bIsInfinity && !rhs.bIsInfinity)
				{
					HNS += rhs.HNS;
				}
				else
				{
					SetToInvalid();
				}
			}
			else
			{
				SetToInvalid();
			}
		}
		return *this;
	}

	inline FTimeValue& operator -= (const FTimeValue& rhs)
	{
		if (bIsValid)
		{
			if (rhs.bIsValid)
			{
				if (!bIsInfinity && !rhs.bIsInfinity)
				{
					HNS -= rhs.HNS;
				}
				else
				{
					SetToInvalid();
				}
			}
			else
			{
				SetToInvalid();
			}
		}
		return *this;
	}

	inline FTimeValue& operator /= (int32 Scale)
	{
		if (bIsValid && !bIsInfinity)
		{
			if (Scale)
			{
				HNS /= Scale;
			}
			else
			{
				SetToPositiveInfinity();
			}
		}
		return *this;
	}

	inline FTimeValue& operator *= (int32 Scale)
	{
		if (bIsValid && !bIsInfinity)
		{
		// FIXME: what if this overflows?
			HNS *= Scale;
		}
		return *this;
	}

	inline FTimeValue operator + (const FTimeValue& rhs) const
	{
		FTimeValue Result;
		if (bIsValid && rhs.bIsValid)
		{
			if (!bIsInfinity && !rhs.bIsInfinity)
			{
				Result.HNS = HNS + rhs.HNS;
				Result.bIsValid = true;
			}
		}
		return Result;
	}

	inline FTimeValue operator - (const FTimeValue& rhs) const
	{
		FTimeValue Result;
		if (bIsValid && rhs.bIsValid)
		{
			if (!bIsInfinity && !rhs.bIsInfinity)
			{
				Result.HNS = HNS - rhs.HNS;
				Result.bIsValid = true;
			}
		}
		return Result;
	}

	inline FTimeValue operator << (int32 Shift) const
	{
		FTimeValue Result(*this);
		if (bIsValid && !bIsInfinity)
		{
			Result.HNS <<= Shift;
		}
		return Result;
	}

	inline FTimeValue operator >> (int32 Shift) const
	{
		FTimeValue Result(*this);
		if (bIsValid && !bIsInfinity)
		{
			Result.HNS >>= Shift;
		}
		return Result;
	}

	inline FTimeValue operator * (int32 Scale) const
	{
		FTimeValue Result(*this);
		if (bIsValid && !bIsInfinity)
		{
			Result.HNS *= Scale;
		}
		return Result;
	}

	inline FTimeValue operator / (int32 Scale) const
	{
		FTimeValue Result(*this);
		if (bIsValid && !bIsInfinity)
		{
			Result.HNS /= Scale;
		}
		return(Result);
	}

private:
	explicit FTimeValue(int64 InHNS) : HNS(InHNS), bIsValid(true), bIsInfinity(false)
	{
	}
	explicit FTimeValue(double Seconds)
	{
		SetFromSeconds(Seconds);
	}

	int64	HNS;
	bool	bIsValid;
	bool	bIsInfinity;
};



struct FTimeRange
{
	void Reset()
	{
		Start.SetToInvalid();
		End.SetToInvalid();
	}
	bool IsValid() const
	{
		return Start.IsValid() && End.IsValid();
	}
	FTimeValue	Start;
	FTimeValue	End;
};



/**
 * Keeps a time value as a fractional.
 */
class FTimeFraction
{
public:
	static const FTimeFraction& GetInvalid()
	{
		static FTimeFraction Invalid;
		return Invalid;
	}

	static const FTimeFraction& GetZero()
	{
		static FTimeFraction Zero(0, 1);
		return Zero;
	}

	FTimeFraction() : Numerator(0), Denominator(0), bIsValid(false)
	{
	}

	FTimeFraction(int64 n, uint32 d) : Numerator(n), Denominator(d), bIsValid(true)
	{
	}

	FTimeFraction(const FTimeFraction& rhs) : Numerator(rhs.Numerator), Denominator(rhs.Denominator), bIsValid(rhs.bIsValid)
	{
	}

	FTimeFraction& operator=(const FTimeFraction& rhs)
	{
		Numerator   = rhs.Numerator;
		Denominator = rhs.Denominator;
		bIsValid	= rhs.bIsValid;
		return *this;
	}

	bool IsValid() const
	{
		return bIsValid;
	}

	bool IsPositiveInfinity() const
	{
		return bIsValid && Denominator==0 && Numerator>=0;
	}

	int64 GetNumerator() const
	{
		return Numerator;
	}

	uint32 GetDenominator() const
	{
		return Denominator;
	}

	double GetAsDouble() const
	{
		return Numerator / (double)Denominator;
	}

	//! Returns this time value in a custom timebase. Requires internal bigint conversion and is therefor SLOW!
	int64 GetAsTimebase(uint32 CustomTimebase) const;

	FTimeFraction& SetFromND(int64 InNumerator, uint32 InDenominator)
	{
		Numerator   = InNumerator;
		Denominator = InDenominator;
		bIsValid	= true;
		return *this;
	}

	FTimeFraction& SetFromFloatString(const FString& In);

private:
	int64	Numerator;
	uint32	Denominator;
	bool	bIsValid;
};


}



/**
 * System wallclock time (UTC)
**/
class MEDIAutcTime
{
public:
	static Electra::FTimeValue Current()
	{
		return Electra::FTimeValue().SetFromMilliseconds(CurrentMSec());
	}
	static int64 CurrentMSec();
};


