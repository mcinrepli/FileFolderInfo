FileFolderInfo — Windows 10 x64 test package

IMPORTANT: this is a test build, not a release. The context-menu DLL and the Ultralight map probe are separate programs. The map is NOT yet integrated into FileFolderInfo or Explorer.

1. Extract the whole archive to a normal local folder. Do not run files from inside the ZIP.
2. To install the Explorer context-menu extension, right-click install.bat and choose Run as administrator. The DLL registers its CLSID and ContextMenuHandlers entries in HKLM. The script asks before restarting Explorer; choose Y to refresh the menu now, or N and restart Explorer manually later.
3. To remove the registration, run uninstall.bat as administrator. Keep FileFolderInfo.dll beside the script until unregistration completes. The script also asks before restarting Explorer.
4. The map is a separate standalone test: open the map-probe folder and run run_probe.bat. It requests OpenStreetMap tiles over the network. Use run_probe_noanim.bat for the comparison run with Leaflet zoom animation disabled. Logs are written beside the probe executable.
5. Test on the intended Windows 10 x64 virtual machine. Report whether the menu registers and appears, whether map tiles load, whether wheel zoom changes the displayed map, and whether resizing the probe window resizes the map. Attach map-probe/ultralight_probe_input.log and map-probe/ultralight_probe.log if present.

Troubleshooting: if registration reports that a module cannot be found, check that the Windows image has the system DLLs imported by FileFolderInfo.dll (including msi.dll). Do not copy Windows system DLLs from this archive or another machine into the system folders. This test does not claim compatibility with a clean Windows installation or other architectures.
