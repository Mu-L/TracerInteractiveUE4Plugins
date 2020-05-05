@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

set PATH_TO_CMAKE_FILE=%CD%\..\

REM Temporary build directories (used as working directories when running CMake)
set CMAKE_EXE="%ANDROID_HOME%\cmake\3.10.2.4988404\bin\cmake.exe"
set ANDROID_TOOLCHAIN_FILE="%PATH_TO_CMAKE_FILE%\Android\Android.cmake"
set ANDROID_BUILD_PATH="%PATH_TO_CMAKE_FILE%\..\Android\Build"
set ANDROID_LIB_PATH="%PATH_TO_CMAKE_FILE%\..\Android"
set MAKE="%NDKROOT%\prebuilt\windows-x86_64\bin\make.exe"

echo Generating vivoxclientapi makefile for Android (64-bit)...
if exist "%ANDROID_BUILD_PATH%" (rmdir "%ANDROID_BUILD_PATH%" /s/q)

echo Building for Android (ARM 64-bit) Release
mkdir "%ANDROID_BUILD_PATH%"
pushd "%ANDROID_BUILD_PATH%" 
%CMAKE_EXE% -G "MinGW Makefiles" -DVIVOXSDK_PATH=../../vivox-sdk/Include -DCMAKE_TOOLCHAIN_FILE="%ANDROID_TOOLCHAIN_FILE%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM=%MAKE% -DCMAKE_BUILD_TYPE=RelWithDebInfo -DANDROID_NATIVE_API_LEVEL=android-21 -DANDROID_ABI=arm64-v8a -DANDROID_STL=c++_shared %PATH_TO_CMAKE_FILE%

REM Now compile it
%MAKE%
mkdir %ANDROID_LIB_PATH%\Release\ARM64
move /y %ANDROID_BUILD_PATH%\..\libvivoxclientapi.a %ANDROID_LIB_PATH%\Release\arm64
popd
rmdir "%ANDROID_BUILD_PATH%" /s/q

echo Building for Android (ARM 64-bit) Debug
mkdir "%ANDROID_BUILD_PATH%"
pushd "%ANDROID_BUILD_PATH%" 
%CMAKE_EXE% -G "MinGW Makefiles" -DVIVOXSDK_PATH=../../vivox-sdk/Include -DCMAKE_TOOLCHAIN_FILE="%ANDROID_TOOLCHAIN_FILE%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM=%MAKE% -DCMAKE_BUILD_TYPE=Debug -DANDROID_NATIVE_API_LEVEL=android-21 -DANDROID_ABI=arm64-v8a -DANDROID_STL=c++_shared %PATH_TO_CMAKE_FILE%

REM Now compile it
%MAKE%
mkdir %ANDROID_LIB_PATH%\Debug\ARM64
move /y %ANDROID_BUILD_PATH%\..\libvivoxclientapi.a %ANDROID_LIB_PATH%\Debug\arm64
popd
rmdir "%ANDROID_BUILD_PATH%" /s/q

echo Building for Android (ARM 32-bit) Release
mkdir "%ANDROID_BUILD_PATH%"
pushd "%ANDROID_BUILD_PATH%" 
%CMAKE_EXE% -G "MinGW Makefiles" -DVIVOXSDK_PATH=../../vivox-sdk/Include -DCMAKE_TOOLCHAIN_FILE="%ANDROID_TOOLCHAIN_FILE%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM=%MAKE% -DCMAKE_BUILD_TYPE=RelWithDebInfo -DANDROID_NATIVE_API_LEVEL=android-19 -DANDROID_ABI=armeabi-v7a -DANDROID_STL=c++_shared %PATH_TO_CMAKE_FILE%

REM Now compile it
%MAKE%
mkdir %ANDROID_LIB_PATH%\Release\ARMv7
move /y %ANDROID_BUILD_PATH%\..\libvivoxclientapi.a %ANDROID_LIB_PATH%\Release\armv7
popd
rmdir "%ANDROID_BUILD_PATH%" /s/q

echo Building for Android (ARM 32-bit) Debug
mkdir "%ANDROID_BUILD_PATH%"
pushd "%ANDROID_BUILD_PATH%" 
%CMAKE_EXE% -G "MinGW Makefiles" -DVIVOXSDK_PATH=../../vivox-sdk/Include -DCMAKE_TOOLCHAIN_FILE="%ANDROID_TOOLCHAIN_FILE%" -DANDROID_NDK="%NDKROOT%" -DCMAKE_MAKE_PROGRAM=%MAKE% -DCMAKE_BUILD_TYPE=Debug -DANDROID_NATIVE_API_LEVEL=android-19 -DANDROID_ABI=armeabi-v7a -DANDROID_STL=c++_shared %PATH_TO_CMAKE_FILE%

REM Now compile it
%MAKE%
mkdir %ANDROID_LIB_PATH%\Debug\ARMv7
move /y %ANDROID_BUILD_PATH%\..\libvivoxclientapi.a %ANDROID_LIB_PATH%\Debug\armv7
popd
rmdir "%ANDROID_BUILD_PATH%" /s/q

