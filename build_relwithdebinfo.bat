set WP=%CD%
cd deps
mkdir build
cd build
set DEPS=%CD%/BambuStudio_dep
if "%1"=="studio" (
    GOTO :studio
)
echo "building deps.."
cmake ../ -G "Visual Studio 16 2019" -DDESTDIR="%CD%/BambuStudio_dep" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --target ALL_BUILD -- -m

if "%1"=="deps" exit /b 0

:studio
echo "building studio..."
cd %WP%
mkdir build 
cd build

cmake .. -G "Visual Studio 16 2019" -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=Release -DWIN10SDK_PATH="C:/Program Files (x86)/Windows Kits/10/Include/10.0.19041.0"
cmake --build . --config RelWithDebInfo --target ALL_BUILD -- -m
@REM cmake --build . --target install --config RelWithDebInfo