#!/bin/bash
set -x -e

# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

# NOTE: this script needs to be built from Engine/Source/ThirdParty/HTML5/Build_All_HTML5_libs.sh


HARFBUZZ_HTML5=$(pwd)


build_via_cmake()
{
	SUFFIX=_O$OLEVEL
	OPTIMIZATION=-O$OLEVEL
	# ----------------------------------------
	rm -rf BUILD$SUFFIX
	mkdir BUILD$SUFFIX
	cd BUILD$SUFFIX
	# ----------------------------------------
#	TYPE=${type^^} # OSX-bash doesn't like this
	TYPE=`echo $type | tr "[:lower:]" "[:upper:]"`
	if [ $TYPE == "DEBUG" ]; then
		DBGFLAG=_DEBUG
	else
		DBGFLAG=NDEBUG
	fi
	EMFLAGS="$UE_EMFLAGS"
	# ----------------------------------------
	emcmake cmake -G "Unix Makefiles" \
		-DBUILD_WITH_FREETYPE_2_6=ON \
		-DUSE_INTEL_ATOMIC_PRIMITIVES=ON \
		-DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=$UE_USE_BITECODE \
		-DCMAKE_BUILD_TYPE=$type \
		-DCMAKE_C_FLAGS_$TYPE="$OPTIMIZATION -D$DBGFLAG $EMFLAGS" \
		../..
	cmake --build . -- harfbuzz -j VERBOSE=1 2>&1 | tee zzz_build.log
	# ----------------------------------------
	if [ $OLEVEL == 0 ]; then
		SUFFIX=
	fi
	dst="../../../HTML5/libharfbuzz${SUFFIX}.$UE_LIB_EXT"
	if [ -e $dst ]; then
		chmod +w $dst
	fi
	cp ../libharfbuzz.$UE_LIB_EXT $dst
	cd ..
}
type=Debug;       OLEVEL=0;  build_via_cmake
type=Release;     OLEVEL=2;  build_via_cmake
type=Release;     OLEVEL=3;  build_via_cmake
type=MinSizeRel;  OLEVEL=z;  build_via_cmake
ls -l ../../HTML5


exit
exit
exit


# NOT USED: LEFT HERE FOR REFERENCE
build_all()
{
	echo
	echo BUILDING $OPTIMIZATION
	echo

	if [ -d $MAKE_PATH$OPTIMIZATION ]; then
		rm -rf $MAKE_PATH$OPTIMIZATION
	fi
	mkdir -p $MAKE_PATH$OPTIMIZATION

	# modify (custom) CMakeLists.txt
	# output library with optimization level appended
	sed -e "s/\(add_library(harfbuzz\)/\1$LIB_SUFFIX/" ../CMakeLists.txt.save > ../CMakeLists.txt

	# modify CMAKE_TOOLCHAIN_FILE
	sed -e "s/\(EPIC_BUILD_FLAGS\} \).*-O2\"/\1$OPTIMIZATION\"/" "$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake" > $MAKE_PATH$OPTIMIZATION/Emscripten.cmake

	#note: this has been merged into Emscripten.cmake (above)
	#EMFLAGS="-msse -msse2 -s FULL_ES2=1 -s USE_PTHREADS=1"

	cd $MAKE_PATH$OPTIMIZATION
		# ./configure
		echo "Generating HarfBuzz makefile..."
		cmake -DCMAKE_TOOLCHAIN_FILE="Emscripten.cmake" -DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=ON \
			-DCMAKE_BUILD_TYPE="Release" -G "Unix Makefiles" ../../BuildForUE

		# make
		echo "Building HarfBuzz..."
		emmake make clean VERBOSE=1
		emmake make VERBOSE=1 | tee log_build.txt

		# make install
#		cp -vp libXXX ../.
	cd -
}
	

build_via_makefile()
{
	# ----------------------------------------
	# using save files so i can run this script over and over again
	
	if [ ! -e ../CMakeLists.txt.save ]; then
		mv ../CMakeLists.txt ../CMakeLists.txt.save
	
		mv ../CMakeLists.txt ../CMakeLists.txt.save
		echo "SET(CMAKE_RELEASE_POSTFIX $LIB_SUFFIX)" >> ../CMakeLists.txt
	fi
	
	
	# ----------------------------------------
	# MAKE
	
}

# no longer needed with latest emscripten:
#		-DCMAKE_TOOLCHAIN_FILE=$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake \

