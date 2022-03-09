#!/bin/bash

# BSD 2-Clause License
#
# Copyright (c) 2021, Christoph Neuhauser, Felix Brendel
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -euo pipefail

SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"
PROJECTPATH="$SCRIPTPATH"
pushd $SCRIPTPATH > /dev/null

debug=true
build_dir_debug=".build_debug"
build_dir_release=".build_release"
destination_dir="Shipping"
if command -v pacman &> /dev/null; then
    is_embree_installed=true
    is_ospray_installed=true
else
    is_embree_installed=false
    is_ospray_installed=false
fi

is_installed_apt() {
    local pkg_name="$1"
    if [ "$(dpkg -l | awk '/'"$pkg_name"'/ {print }'|wc -l)" -ge 1 ]; then
        return 0
    else
        return 1
    fi
}

is_installed_pacman() {
    local pkg_name="$1"
    if pacman -Qs $pkg_name > /dev/null; then
        return 0
    else
        return 1
    fi
}

if command -v apt &> /dev/null; then
    if ! command -v cmake &> /dev/null || ! command -v git &> /dev/null || ! command -v curl &> /dev/null \
            || ! command -v pkg-config &> /dev/null || ! command -v g++ &> /dev/null; then
        echo "------------------------"
        echo "installing build essentials"
        echo "------------------------"
        sudo apt install cmake git curl pkg-config build-essential
    fi

    # Dependencies of sgl and CloudRendering.
    if ! is_installed_apt "libglm-dev" || ! is_installed_apt "libsdl2-dev" || ! is_installed_apt "libsdl2-image-dev" \
            || ! is_installed_apt "libpng-dev" || ! is_installed_apt "libboost-filesystem-dev" \
            || ! is_installed_apt "libtinyxml2-dev" || ! is_installed_apt "libarchive-dev" \
            || ! is_installed_apt "libglew-dev" || ! is_installed_apt "libjsoncpp-dev" \
            || ! is_installed_apt "libopenexr-dev"; then
        echo "------------------------"
        echo "installing dependencies "
        echo "------------------------"
        sudo apt install libglm-dev libsdl2-dev libsdl2-image-dev libpng-dev libboost-filesystem-dev libtinyxml2-dev \
        libarchive-dev libglew-dev libjsoncpp-dev libopenexr-dev
    fi
elif command -v pacman &> /dev/null; then
    if ! command -v cmake &> /dev/null || ! command -v git &> /dev/null || ! command -v curl &> /dev/null \
            || ! command -v pkg-config &> /dev/null || ! command -v g++ &> /dev/null; then
        echo "------------------------"
        echo "installing build essentials"
        echo "------------------------"
        sudo pacman -S cmake git curl pkgconf base-devel
    fi

    # Dependencies of sgl and CloudRendering.
    if ! is_installed_pacman "boost" || ! is_installed_pacman "libarchive" || ! is_installed_pacman "glm" \
            || ! is_installed_pacman "tinyxml2" || ! is_installed_pacman "sdl2" \
            || ! is_installed_pacman "sdl2_image" || ! is_installed_pacman "glew" \
            || ! is_installed_pacman "vulkan-devel" || ! is_installed_pacman "shaderc" \
            || ! is_installed_pacman "jsoncpp" || ! is_installed_pacman "openexr"; then
        echo "------------------------"
        echo "installing dependencies "
        echo "------------------------"
        sudo pacman -S boost libarchive glm tinyxml2 sdl2 sdl2_image glew vulkan-devel shaderc jsoncpp openexr
    fi
else
    echo "Warning: Unsupported system package manager detected." >&2
fi


if ! command -v cmake &> /dev/null; then
    echo "CMake was not found, but is required to build the program."
    exit 1
fi
if ! command -v git &> /dev/null; then
    echo "git was not found, but is required to build the program."
    exit 1
fi
if ! command -v curl &> /dev/null; then
    echo "curl was not found, but is required to build the program."
    exit 1
fi
if ! command -v pkg-config &> /dev/null; then
    echo "pkg-config was not found, but is required to build the program."
    exit 1
fi

if [ ! -d "submodules/IsosurfaceCpp/src" ]; then
    echo "------------------------"
    echo "initializing submodules "
    echo "------------------------"
    git submodule init
    git submodule update
fi

[ -d "./third_party/" ] || mkdir "./third_party/"
pushd third_party > /dev/null

if [[ ! -v VULKAN_SDK ]]; then
    echo "------------------------"
    echo "searching for Vulkan SDK"
    echo "------------------------"

    found_vulkan=false

    if lsb_release -a 2> /dev/null | grep -q 'Ubuntu'; then
        if ! compgen -G "/etc/apt/sources.list.d/lunarg-vulkan-*" > /dev/null; then
            distro_code_name=$(lsb_release -c | grep -oP "\:\s+\K\S+")
            echo "Setting up Vulkan SDK for Ubuntu $(lsb_release -r | grep -oP "\:\s+\K\S+")..."
            wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
            sudo curl --silent --show-error --fail \
            https://packages.lunarg.com/vulkan/1.2.198/lunarg-vulkan-1.2.198-${distro_code_name}.list \
            --output /etc/apt/sources.list.d/lunarg-vulkan-1.2.198-${distro_code_name}.list
            sudo apt update
            sudo apt install vulkan-sdk shaderc
        fi
    fi

    if [ -d "/usr/include/vulkan" ]; then
        if ! grep -q VULKAN_SDK ~/.bashrc; then
            echo 'export VULKAN_SDK="/usr"' >> ~/.bashrc
        fi
        VULKAN_SDK="/usr"
        found_vulkan=true
    fi

    if ! $found_vulkan; then
        echo "The environment variable VULKAN_SDK is not set but is required in the installation process."
        echo "Please refer to https://vulkan.lunarg.com/sdk/home#linux for instructions on how to install the Vulkan SDK."
        exit 1
    fi
fi

if [ ! -d "./sgl" ]; then
    echo "------------------------"
    echo "     fetching sgl       "
    echo "------------------------"
    git clone --depth 1 https://github.com/chrismile/sgl.git
fi

if [ ! -d "./sgl/install" ]; then
    echo "------------------------"
    echo "     building sgl       "
    echo "------------------------"

    pushd "./sgl" >/dev/null
    mkdir -p .build_debug
    mkdir -p .build_release

    pushd "$build_dir_debug" >/dev/null
    cmake .. \
         -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_INSTALL_PREFIX="../install"
    make -j
    make install
    popd >/dev/null

    pushd $build_dir_release >/dev/null
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="../install"
    make -j
    make install
    popd >/dev/null

    popd >/dev/null
fi

params=()

embree_version="3.13.2"
if ! $is_embree_installed && [ ! -d "./embree-${embree_version}.x86_64.linux" ]; then
    echo "------------------------"
    echo "   downloading Embree   "
    echo "------------------------"
    wget "https://github.com/embree/embree/releases/download/v${embree_version}/embree-${embree_version}.x86_64.linux.tar.gz"
    tar -xvzf "embree-${embree_version}.x86_64.linux.tar.gz"
    params+=(-Dembree_DIR=${PROJECTPATH}/third_party/embree-${embree_version}.x86_64.linux/lib/cmake/embree-${embree_version})
fi

ospray_version="2.8.0"
if ! $is_ospray_installed && [ ! -d "./ospray-${ospray_version}.x86_64.linux" ]; then
    echo "------------------------"
    echo "   downloading OSPRay   "
    echo "------------------------"
    wget "https://github.com/ospray/OSPRay/releases/download/v${ospray_version}/ospray-${ospray_version}.x86_64.linux.tar.gz"
    tar -xvzf "ospray-${ospray_version}.x86_64.linux.tar.gz"
    params+=(-Dospray_DIR=${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/cmake/ospray-${ospray_version})
fi

popd >/dev/null # back to project root

if [ $debug = true ] ; then
    echo "------------------------"
    echo "  building in debug     "
    echo "------------------------"

    cmake_config="Debug"
    build_dir=$build_dir_debug
else
    echo "------------------------"
    echo "  building in release   "
    echo "------------------------"

    cmake_config="Release"
    build_dir=$build_dir_release
fi
mkdir -p $build_dir
ls "$build_dir"

echo "------------------------"
echo "      generating        "
echo "------------------------"
pushd $build_dir >/dev/null
cmake .. \
    -DCMAKE_BUILD_TYPE=$cmake_config \
    -Dsgl_DIR="$PROJECTPATH/third_party/sgl/install/lib/cmake/sgl/" \
    "${params[@]}"
popd >/dev/null

echo "------------------------"
echo "      compiling         "
echo "------------------------"
pushd "$build_dir" >/dev/null
make -j
popd >/dev/null

echo ""
echo "All done!"


pushd $build_dir >/dev/null

if [[ -z "${LD_LIBRARY_PATH+x}" ]]; then
    export LD_LIBRARY_PATH="${PROJECTPATH}/third_party/sgl/install/lib"
elif [[ ! "${LD_LIBRARY_PATH}" == *"${PROJECTPATH}/third_party/sgl/install/lib"* ]]; then
    export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${PROJECTPATH}/third_party/sgl/install/lib"
fi
./CloudRendering

