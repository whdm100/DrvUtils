cd /d "D:\Project\Driver\DriverDemo\ShadowVolume" &msbuild "ShadowVolume.vcxproj" /t:sdvViewer /p:configuration="Debug" /p:platform=x64
exit %errorlevel% 