This solution builds libs and/or dlls (depending on output configuration) to wrap Windows Mixed Reality winrt apis available in Windows 10 SDKs and one header to provide version info for UE4.

To build new interop binaries:
1) Open for edit the target files.  p4EditBinaries.bat will use BinaryFileList.txt to do this with perforce.
2) Right click solution and "restore nuget packages" if they did not automatically download (the automatic download has been increasingly reliable).
3) Build MixedRealtityInterop x64 debug and release, MixedRealityInteropHoloLens x64 and arm64 debug and release and submit the results to perforce if you change the implementation. The visual studio 'Build->Batch Build' feature is useful here (check all the boxes).
4) Submit the target files along with whatever changes made it necessary to build.
*Note: there appears to be a file load race condition or similar in Visual Studio 2019 related to winrt nuget packages.  If one or a few configurations fail to build with winrt related errors you may simply need to close visual studio and open it again to get a successful build (I think doing this once has always worked for me).  Once you get one full success that instance of visual studio 2019 will continue to build successfully. **This problem may have gone away when we went to full nuget rather than our previous half-nuget setup.

To update any nuget package, or the windows sdk version you will need to do the following:
1) Open for edit the target files.  p4EditBinaries.bat will use BinaryFileList.txt to do this with perforce.
2) Update the remoting nuget package via 'manage nuget packages...' in the project context menu for the MixedRealityInterop project.  AND/OR update the target platform version in the project files.
3) Rebuild the interop binaries.

If a new package version adds/removes/renames any output binary files you will need to:
1) Add to/remove from Perforce.
2) Update BinaryFileList.txt as necessary.
3) Update MicrosoftAzureSpatialAnchorsForWMR.Build.cs for which files to package.
4) Update MicrosoftAzureSpatialAnchorsForWMR.cpp for which files to explicitly load at runtime.


After this you would build UE4.