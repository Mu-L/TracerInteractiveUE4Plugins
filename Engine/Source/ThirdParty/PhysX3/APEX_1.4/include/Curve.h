/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef CURVE_H
#define CURVE_H

#include "Apex.h"
#include "nvparameterized/NvParameterized.h"

namespace nvidia
{
namespace apex
{

PX_PUSH_PACK_DEFAULT

/**
	\brief A trivial templatized math vector type for pairs.
*/
template <typename T>
struct Vec2T
{
	/**
	\brief Constructor that takes 0, 1, or 2 parameters to initialize the pair
	*/
	Vec2T(T _x = T(), T _y = T()) : x(_x), y(_y) { }

	/**
	\brief The first element in the pair
	*/
	T x;

	/**
	\brief The second element in the pair
	*/	T y;

	/**
	\brief Overloading the subscript operator to access the members of the pair
	*/	
	T& operator[](uint32_t ndx)
	{
		PX_ASSERT(ndx < 2);
		return ((T*)&x)[ndx];
	}
};

/**
\brief Vec2R is a helpful typedef for a pair of 32bit floats.
*/
typedef Vec2T<float> Vec2R;

/**
	The Curve is a class for storing control points on a curve and evaluating the results later.
*/
class Curve
{
public:
	virtual ~Curve() {}

	/**
		Retrieve the output Y for the specified input x, based on the properties of the stored curve described
		by mControlPoints.
	*/
	virtual float evaluate(float x) const = 0;

	/**
		Add a control point to the list of control points, returning the index of the new point.
	*/
	virtual uint32_t addControlPoint(const Vec2R& controlPoint) = 0;

	/**
		Add a control points to the list of control points.  Assuming the
		hPoints points to a list of vec2s
	*/
	virtual void addControlPoints(::NvParameterized::Interface* param, ::NvParameterized::Handle& hPoints) = 0;

	/**
		Locates the control points that contain x, placing the resulting control points in the two
		out parameters. Returns true if the points were found, false otherwise. If the points were not
		found, the output variables are untouched
	*/
	virtual bool calculateControlPoints(float x, Vec2R& outXPoints, Vec2R& outYPoints) const = 0;

	/**
		Locates the first control point with x larger than xValue or the nimber of control points if such point doesn't exist
	*/
	virtual uint32_t calculateFollowingControlPoint(float xValue) const = 0;

	///get the array of control points
	virtual const Vec2R* getControlPoints(uint32_t& outCount) const = 0;
};

PX_POP_PACK

}
} // namespace apex

#endif // CURVE_H
