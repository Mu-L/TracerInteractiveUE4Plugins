:: Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
@echo off

pushd %~dp0

call setup.bat

title Matchmaker

::Run node server
node matchmaker %*

popd
pause