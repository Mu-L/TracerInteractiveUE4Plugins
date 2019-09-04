#!/bin/bash
## Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
##
## Unreal Engine 4 AutomationTool setup script
##
## This script is expecting to exist in the UE4/Engine/Build/BatchFiles directory.  It will not work
## correctly if you copy it to a different location and run it.

echo
echo Running AutomationTool...
echo

TermHandler() {
	GPID=$(ps -o pgid= $UATPid)
	kill -TERM -$GPID 2> /dev/null
}

# loop over the arguments, quoting spaces to pass to UAT proper
Args=
i=0
for Arg in "$@"
do
	# replace all ' ' with '\ '
	# DISABLED UNTIL FURTHER INVESTIGATION - IT SEEMS IT WASN'T NEEDED AFTER ALL
	# NewArg=${Arg// /\\ }
	NewArg=$Arg
	# append it to the array
	Args[i]=$NewArg
	# move to next array entry
	i=$((i+1))
done


# put ourselves into Engine directory (two up from location of this script)
SCRIPT_DIR=$(cd "`dirname "$0"`" && pwd)
cd "$SCRIPT_DIR/../.."

UATDirectory=Binaries/DotNET/
UATCompileArg=-compile

if [ ! -f Build/BatchFiles/RunUAT.sh ]; then
	echo "RunUAT ERROR: The script does not appear to be located in the "
  echo "Engine/Build/BatchFiles directory.  This script must be run from within that directory."
	exit 1
fi

# see if we have the no compile arg
if echo "${Args[@]}" | grep -q -w -i "\-nocompile"; then
	UATCompileArg=
else
	UATCompileArg=-compile
fi

if [ -f Build\InstalledBuild.txt ]; then
	UATCompileArg=
fi

if [ "$(uname)" = "Darwin" ]; then
	# Setup Mono
	source "$SCRIPT_DIR/Mac/SetupMono.sh" "$SCRIPT_DIR/Mac"
fi

if [ "$(uname)" = "Linux" ]; then
	# Setup Mono
	source "$SCRIPT_DIR/Linux/SetupMono.sh" "$SCRIPT_DIR/Linux"
fi


if [ "$UATCompileArg" = "-compile" ]; then
  # see if the .csproj exists to be compiled
	if [ ! -f Source/Programs/AutomationTool/AutomationTool.csproj ]; then
		echo No project to compile, attempting to use precompiled AutomationTool
		UATCompileArg=
	else

		# make sure the UBT project has references to auto-discovered platform extension source files
		"${SCRIPT_DIR}/FindPlatformExtensionSources.sh"

		# mono 5.0 and up include msbuild
		if [ "$IS_MS_BUILD_AVAILABLE" == "1" ]; then
			BUILD_TOOL=msbuild
		else
			BUILD_TOOL=xbuild
		fi

		ARGS="/p:Configuration=Development /p:Platform=AnyCPU /verbosity:quiet /nologo /p:NoWarn=1591 /property:AutomationToolProjectOnly=true"
		ARGS="${ARGS} /p:TargetFrameworkProfile="

		echo "$BUILD_TOOL Source/Programs/AutomationTool/AutomationTool.csproj $ARGS"
		$BUILD_TOOL Source/Programs/AutomationTool/AutomationTool.csproj $ARGS
		# make sure it succeeded
		if [ $? -ne 0 ]; then
			echo RunUAT ERROR: AutomationTool failed to compile.
			exit 1
		fi
	fi
fi

## Run AutomationTool

#run UAT
cd $UATDirectory
if [ -z "$uebp_LogFolder" ]; then
  LogDir="$HOME/Library/Logs/Unreal Engine/LocalBuildLogs"
else
	LogDir="$uebp_LogFolder"
fi

trap TermHandler SIGTERM SIGINT

# you can't set a dotted env var nicely in sh, but env will run a command with
# a list of env vars set, including dotted ones
echo Start UAT: mono AutomationTool.exe "${Args[@]}"
env uebp_LogFolder="$LogDir" mono AutomationTool.exe "${Args[@]}" $UATCompileArg &
UATPid=$!
wait $UATPid
UATReturn=$?

# @todo: Copy log files to somewhere useful
# if not "%uebp_LogFolder%" == "" copy log*.txt %uebp_LogFolder%\UAT_*.*
# if "%uebp_LogFolder%" == "" copy log*.txt c:\LocalBuildLogs\UAT_*.*
#cp log*.txt /var/log

if [ $UATReturn -ne 0 ]; then
	echo RunUAT ERROR: AutomationTool was unable to run successfully.
	exit $UATReturn
fi
