///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

#ifndef INCLUDED_IMATHNAMESPACE_H
#define INCLUDED_IMATHNAMESPACE_H

//
// The purpose of this file is to make it possible to specify an
// IMATH_INTERNAL_NAMESPACE as a preprocessor definition and have all of the
// Imath symbols defined within that namespace rather than the standard
// Imath namespace.  Those symbols are made available to client code through
// the IMATH_NAMESPACE in addition to the IMATH_INTERNAL_NAMESPACE.
//
// To ensure source code compatibility, the IMATH_NAMESPACE defaults to Imath
// and then "using namespace IMATH_INTERNAL_NAMESPACE;" brings all of the
// declarations from the IMATH_INTERNAL_NAMESPACE into the IMATH_NAMESPACE.
// This means that client code can continue to use syntax like Imath::V3f,
// but at link time it will resolve to a mangled symbol based on the
// IMATH_INTERNAL_NAMESPACE.
//
// As an example, if one needed to build against a newer version of Imath and
// have it run alongside an older version in the same application, it is now
// possible to use an internal namespace to prevent collisions between the
// older versions of Imath symbols and the newer ones.  To do this, the
// following could be defined at build time:
//
// IMATH_INTERNAL_NAMESPACE = Imath_v2
//
// This means that declarations inside Imath headers look like this (after
// the preprocessor has done its work):
//
// namespace Imath_v2 {
//     ...
//     class declarations
//     ...
// }
//
// namespace Imath {
//     using namespace Imath_v2;
// }
//

//
// Open Source version of this file pulls in the IlmBaseConfig.h file
// for the configure time options.
//
#include "IlmBaseConfig.h"


#ifndef IMATH_NAMESPACE
#define IMATH_NAMESPACE Imath
#endif

#ifndef IMATH_INTERNAL_NAMESPACE
#define IMATH_INTERNAL_NAMESPACE IMATH_NAMESPACE
#endif

//
// We need to be sure that we import the internal namespace into the public one.
// To do this, we use the small bit of code below which initially defines
// IMATH_INTERNAL_NAMESPACE (so it can be referenced) and then defines
// IMATH_NAMESPACE and pulls the internal symbols into the public
// namespace.
//

//
// There are identical pairs of HEADER/SOURCE ENTER/EXIT macros so that
// future extension to the namespace mechanism is possible without changing
// project source code.
//

#define IMATH_INTERNAL_NAMESPACE_HEADER_ENTER namespace IMATH_INTERNAL_NAMESPACE {
#define IMATH_INTERNAL_NAMESPACE_HEADER_EXIT }

#define IMATH_INTERNAL_NAMESPACE_SOURCE_ENTER namespace IMATH_INTERNAL_NAMESPACE {
#define IMATH_INTERNAL_NAMESPACE_SOURCE_EXIT }


#endif /* INCLUDED_IMATHNAMESPACE_H */
