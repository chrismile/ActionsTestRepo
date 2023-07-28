:: BSD 2-Clause License
::
:: Copyright (c) 2021-2022, Christoph Neuhauser, Felix Brendel
:: All rights reserved.
::
:: Redistribution and use in source and binary forms, with or without
:: modification, are permitted provided that the following conditions are met:
::
:: 1. Redistributions of source code must retain the above copyright notice, this
::    list of conditions and the following disclaimer.
::
:: 2. Redistributions in binary form must reproduce the above copyright notice,
::    this list of conditions and the following disclaimer in the documentation
::    and/or other materials provided with the distribution.
::
:: THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
:: AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
:: IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
:: DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
:: FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
:: DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
:: SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
:: CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
:: OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
:: OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

@echo off
setlocal
pushd %~dp0

set vcpkg_triplet="x64-windows"
set build_with_zarr_support=true
set build_with_osqp_support=true

:loop
IF NOT "%1"=="" (
    IF "%1"=="--vcpkg-triplet" (
        SET vcpkg_triplet=%2
        SHIFT
    )
    SHIFT
    GOTO :loop
)

:: Creates a string with, e.g., -G "Visual Studio 17 2022".
:: Needs to be run from a Visual Studio developer PowerShell or command prompt.
if defined VCINSTALLDIR (
    set VCINSTALLDIR_ESC=%VCINSTALLDIR:\=\\%
)
if defined VCINSTALLDIR (
    set "x=%VCINSTALLDIR_ESC:Microsoft Visual Studio\\=" & set "VsPathEnd=%"
)
if defined VCINSTALLDIR (
    set cmake_generator=-G "Visual Studio %VisualStudioVersion:~0,2% %VsPathEnd:~0,4%"
)
if not defined VCINSTALLDIR (
    set cmake_generator=
)

set third_party_dir=%~dp0/third_party
set cmake_args=-DCMAKE_TOOLCHAIN_FILE="third_party/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
               -DVCPKG_TARGET_TRIPLET=%vcpkg_triplet%                                      ^
               -DCMAKE_CXX_FLAGS="/MP"                                                     ^
               -Dsgl_DIR="third_party/sgl/install/lib/cmake/sgl/"

set cmake_args_general=-DCMAKE_TOOLCHAIN_FILE="%third_party_dir%/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
               -DVCPKG_TARGET_TRIPLET=%vcpkg_triplet%

if not exist .\third_party\ mkdir .\third_party\
pushd third_party

if %build_with_zarr_support% == true (
    if not exist ".\xtl" (
        echo ------------------------
        echo     downloading xtl
        echo ------------------------
        :: Make sure we have no leftovers from a failed build attempt.
        if exist ".\xtl-src" (
            rmdir /s /q ".\xtl-src"
        )
        git clone https://github.com/xtensor-stack/xtl.git xtl-src
        if not exist .\xtl-src\build\ mkdir .\xtl-src\build\
        pushd "xtl-src\build"
        cmake %cmake_generator% %cmake_args_general% ^
        -DCMAKE_INSTALL_PREFIX="%third_party_dir%/xtl" ..
        cmake --build . --config Release --target install || exit /b 1
        popd
    )
    if not exist ".\xtensor" (
        echo ------------------------
        echo   downloading xtensor
        echo ------------------------
        :: Make sure we have no leftovers from a failed build attempt.
        if exist ".\xtensor-src" (
            rmdir /s /q ".\xtensor-src"
        )
        git clone https://github.com/xtensor-stack/xtensor.git xtensor-src
        if not exist .\xtensor-src\build\ mkdir .\xtensor-src\build\
        pushd "xtensor-src\build"
        cmake %cmake_generator% %cmake_args_general% ^
        -Dxtl_DIR="%third_party_dir%/xtl/share/cmake/xtl" ^
        -DCMAKE_INSTALL_PREFIX="%third_party_dir%/xtensor" ..
        cmake --build . --config Release --target install || exit /b 1
        popd
    )
    if not exist ".\xsimd" (
        echo ------------------------
        echo    downloading xsimd
        echo ------------------------
        :: Make sure we have no leftovers from a failed build attempt.
        if exist ".\xsimd-src" (
            rmdir /s /q ".\xsimd-src"
        )
        git clone https://github.com/xtensor-stack/xsimd.git xsimd-src
        if not exist .\xsimd-src\build\ mkdir .\xsimd-src\build\
        pushd "xsimd-src\build"
        cmake %cmake_generator% %cmake_args_general% ^
        -Dxtl_DIR="%third_party_dir%/xtl/share/cmake/xtl" ^
        -DENABLE_XTL_COMPLEX=ON ^
        -DCMAKE_INSTALL_PREFIX="%third_party_dir%/xsimd" ..
        cmake --build . --config Release --target install || exit /b 1
        popd
    )
    if not exist ".\z5" (
        echo ------------------------
        echo      downloading z5
        echo ------------------------
        :: Make sure we have no leftovers from a failed build attempt.
        if exist ".\z5-src" (
            rmdir /s /q ".\z5-src"
        )
        git clone https://github.com/constantinpape/z5.git z5-src
        :: sed -i '/^SET(Boost_NO_SYSTEM_PATHS ON)$/s/^/#/' z5-src/CMakeLists.txt
        powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'SET\(Boost_NO_SYSTEM_PATHS ON\)', '#SET(Boost_NO_SYSTEM_PATHS ON)' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'SET\(BOOST_ROOT \"\$\{CMAKE_PREFIX_PATH\}\/Library\"\)', '#SET(BOOST_ROOT \"${CMAKE_PREFIX_PATH}/Library\")' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'SET\(BOOST_LIBRARYDIR \"\$\{CMAKE_PREFIX_PATH\}\/Library\/lib\"\)', '#SET(BOOST_LIBRARYDIR \"${CMAKE_PREFIX_PATH}/Library/lib\")' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        :: powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'find_package\(Boost 1.63.0 COMPONENTS system filesystem REQUIRED\)', 'find_package(Boost COMPONENTS system filesystem REQUIRED)' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        :: powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'find_package\(Boost 1.63.0 REQUIRED\)', 'find_package(Boost REQUIRED)' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'set\(CMAKE_MODULE_PATH \$\{CMAKE_CURRENT_SOURCE_DIR\}\/cmake\)', 'list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'set\(CMAKE_PREFIX_PATH \$\{CMAKE_PREFIX_PATH\} CACHE PATH \"\"\)', '#set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} CACHE PATH "")' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        powershell -Command "(gc z5-src/CMakeLists.txt) -replace 'if\(NOT WITHIN_TRAVIS\)', 'if(FALSE)' | Out-File -encoding ASCII z5-src/CMakeLists.txt"
        echo {> %third_party_dir%/z5-src/vcpkg.json
        echo "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg/master/scripts/vcpkg.schema.json",>> %third_party_dir%/z5-src/vcpkg.json
        echo "name": "z5",>> %third_party_dir%/z5-src/vcpkg.json
        echo "version": "0.1.0",>> %third_party_dir%/z5-src/vcpkg.json
        echo "dependencies": [ "boost-core", "boost-filesystem", "nlohmann-json", "blosc" ]>> %third_party_dir%/z5-src/vcpkg.json
        echo }>> %third_party_dir%/z5-src/vcpkg.json
        if not exist .\z5-src\build\ mkdir .\z5-src\build\
        pushd "z5-src\build"
        cmake %cmake_generator% %cmake_args_general% ^
        -Dxtl_DIR="%third_party_dir%/xtl/share/cmake/xtl" ^
        -Dxtensor_DIR="%third_party_dir%/xtensor/share/cmake/xtensor" ^
        -Dxsimd_DIR="%third_party_dir%/xsimd/lib/cmake/xsimd" ^
        -DBUILD_Z5PY=OFF -DWITH_ZLIB=ON -DWITH_LZ4=ON -DWITH_BLOSC=ON ^
        -DCMAKE_INSTALL_PREFIX="%third_party_dir%/z5" ..
        cmake --build . --config Release --target install || exit /b 1
        popd
    )
)
set cmake_args=%cmake_args% -Dxtl_DIR="third_party/xtl/share/cmake/xtl" ^
-Dxtensor_DIR="third_party/xtensor/share/cmake/xtensor" ^
-Dxsimd_DIR="third_party/xsimd/lib/cmake/xsimd" ^
-Dz5_DIR="third_party/z5/lib/cmake/z5"

popd

