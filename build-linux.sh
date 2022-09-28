#!/bin/bash

# BSD 2-Clause License
#
# Copyright (c) 2021-2022, Christoph Neuhauser, Felix Brendel
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

debug=false
build_dir_debug=".build_debug"
build_dir_release=".build_release"
if [ $debug = true ]; then
    cmake_config="Debug"
    build_dir=$build_dir_debug
else
    cmake_config="Release"
    build_dir=$build_dir_release
fi
destination_dir="Shipping"
if command -v pacman &> /dev/null; then
    is_embree_installed=true
    is_ospray_installed=true
else
    is_embree_installed=false
    is_ospray_installed=false
fi

# Process command line arguments.
custom_glslang=false
for ((i=1;i<=$#;i++));
do
    if [ ${!i} = "--custom-glslang" ]; then
        custom_glslang=true
    fi
done

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

is_installed_yay() {
    local pkg_name="$1"
    if yay -Ss $pkg_name > /dev/null | grep -q 'instal'; then
        return 1
    else
        return 0
    fi
}

is_installed_yum() {
    local pkg_name="$1"
    if yum list installed "$pkg_name" > /dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

is_installed_rpm() {
    local pkg_name="$1"
    if rpm -q "$pkg_name" > /dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

if command -v apt &> /dev/null; then
    if ! command -v cmake &> /dev/null || ! command -v git &> /dev/null || ! command -v curl &> /dev/null \
            || ! command -v pkg-config &> /dev/null || ! command -v g++ &> /dev/null \
            || ! command -v patchelf &> /dev/null; then
        echo "------------------------"
        echo "installing build essentials"
        echo "------------------------"
        sudo apt install -y cmake git curl pkg-config build-essential patchelf
    fi

    # Dependencies of sgl and LineVis.
    if ! is_installed_apt "libglm-dev" || ! is_installed_apt "libsdl2-dev" || ! is_installed_apt "libsdl2-image-dev" \
            || ! is_installed_apt "libpng-dev" || ! is_installed_apt "libboost-filesystem-dev" \
            || ! is_installed_apt "libtinyxml2-dev" || ! is_installed_apt "libarchive-dev" \
            || ! is_installed_apt "libglew-dev" || ! is_installed_apt "opencl-c-headers" \
            || ! is_installed_apt "ocl-icd-opencl-dev" \
            || ! is_installed_apt "libjsoncpp-dev" || ! is_installed_apt "libeigen3-dev" \
            || ! is_installed_apt "python3-dev" || ! is_installed_apt "python3-numpy" \
            || ! is_installed_apt "libzmq3-dev" || ! is_installed_apt "libnetcdf-dev" \
            || ! is_installed_apt "libopenexr-dev" || ! is_installed_apt "libeccodes-dev" \
            || ! is_installed_apt "libeccodes-tools"  || ! is_installed_apt "libopenjp2-7-dev"; then
        echo "------------------------"
        echo "installing dependencies "
        echo "------------------------"
        sudo apt install -y libglm-dev libsdl2-dev libsdl2-image-dev libpng-dev libboost-filesystem-dev libtinyxml2-dev \
        libarchive-dev libglew-dev opencl-c-headers ocl-icd-opencl-dev libjsoncpp-dev libeigen3-dev python3-dev \
        python3-numpy libzmq3-dev libnetcdf-dev libopenexr-dev libeccodes-dev libeccodes-tools libopenjp2-7-dev
    fi
elif command -v pacman &> /dev/null; then
    if ! command -v cmake &> /dev/null || ! command -v git &> /dev/null || ! command -v curl &> /dev/null \
            || ! command -v pkg-config &> /dev/null || ! command -v g++ &> /dev/null \
            || ! command -v patchelf &> /dev/null; then
        echo "------------------------"
        echo "installing build essentials"
        echo "------------------------"
        sudo pacman -S cmake git curl pkgconf base-devel patchelf
    fi

    # Dependencies of sgl and LineVis.
    if ! is_installed_pacman "boost" || ! is_installed_pacman "libarchive" \
            || ! is_installed_pacman "glm" || ! is_installed_pacman "tinyxml2" \
            || ! is_installed_pacman "sdl2" || ! is_installed_pacman "sdl2_image" \
            || ! is_installed_pacman "glew" || ! is_installed_pacman "vulkan-devel" \
            || ! is_installed_pacman "shaderc" || ! is_installed_pacman "opencl-headers" \
            || ! is_installed_pacman "ocl-icd" \
            || ! is_installed_pacman "python3" || ! is_installed_pacman "python-numpy" \
            || ! is_installed_pacman "eigen" || ! is_installed_pacman "jsoncpp" \
            || ! is_installed_pacman "zeromq" || ! is_installed_pacman "netcdf" \
            || ! is_installed_pacman "ospray" || ! is_installed_pacman "openexr"; then
        echo "------------------------"
        echo "installing dependencies "
        echo "------------------------"
        sudo pacman -S boost libarchive glm tinyxml2 sdl2 sdl2_image glew vulkan-devel shaderc opencl-headers ocl-icd \
        python3 python-numpy eigen jsoncpp zeromq netcdf ospray openexr
    fi
    if command -v yay &> /dev/null && ! is_installed_yay "eccodes"; then
        yay -S eccodes
    fi
elif command -v yum &> /dev/null; then
    if ! command -v cmake &> /dev/null || ! command -v git &> /dev/null || ! command -v curl &> /dev/null \
            || ! command -v pkg-config &> /dev/null || ! command -v g++ &> /dev/null \
            || ! command -v patchelf &> /dev/null; then
        echo "------------------------"
        echo "installing build essentials"
        echo "------------------------"
        sudo yum install -y cmake git curl pkgconf gcc gcc-c++ patchelf
    fi

    # Dependencies of sgl and LineVis.
    if ! is_installed_rpm "boost-devel" || ! is_installed_rpm "libarchive-devel" \
            || ! is_installed_rpm "glm-devel" || ! is_installed_rpm "tinyxml2-devel" \
            || ! is_installed_rpm "SDL2-devel" || ! is_installed_rpm "SDL2_image-devel" \
            || ! is_installed_rpm "libpng-devel" || ! is_installed_rpm "glew-devel" \
            || ! is_installed_rpm "vulkan-headers" || ! is_installed_rpm "libshaderc-devel" \
            || ! is_installed_rpm "opencl-headers" || ! is_installed_rpm "ocl-icd" \
            || ! is_installed_rpm "python3-devel" || ! is_installed_rpm "python3-numpy" \
            || ! is_installed_rpm "eigen3-devel" || ! is_installed_rpm "jsoncpp-devel" \
            || ! is_installed_rpm "zeromq-devel" || ! is_installed_rpm "cppzmq-devel" \
            || ! is_installed_rpm "netcdf-devel" || ! is_installed_rpm "openexr-devel" \
            || ! is_installed_rpm "eccodes-devel"; then
        echo "------------------------"
        echo "installing dependencies "
        echo "------------------------"
        sudo yum install -y boost-devel libarchive-devel glm-devel tinyxml2-devel SDL2-devel SDL2_image-devel \
        libpng-devel glew-devel vulkan-headers libshaderc-devel opencl-headers ocl-icd python3-devel python3-numpy \
        eigen3-devel jsoncpp-devel zeromq-devel cppzmq-devel netcdf-devel openexr-devel eccodes-devel
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

os_arch="$(uname -m)"
if [[ ! -v VULKAN_SDK ]]; then
    echo "------------------------"
    echo "searching for Vulkan SDK"
    echo "------------------------"

    found_vulkan=false

    if [ -d "VulkanSDK" ]; then
        VK_LAYER_PATH=""
        source "VulkanSDK/$(ls VulkanSDK)/setup-env.sh"
        export PKG_CONFIG_PATH="$(realpath "VulkanSDK/$(ls VulkanSDK)/$os_arch/lib/pkgconfig")"
        found_vulkan=true
    fi

    if ! $found_vulkan && lsb_release -a 2> /dev/null | grep -q 'Ubuntu'; then
        distro_code_name=$(lsb_release -c | grep -oP "\:\s+\K\S+")
        if ! compgen -G "/etc/apt/sources.list.d/lunarg-vulkan-*" > /dev/null \
              && ! curl -s -I "https://packages.lunarg.com/vulkan/lunarg-vulkan-${distro_code_name}.list" | grep "2 404" > /dev/null; then
            echo "Setting up Vulkan SDK for Ubuntu $(lsb_release -r | grep -oP "\:\s+\K\S+")..."
            wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
            sudo curl --silent --show-error --fail \
            https://packages.lunarg.com/vulkan/lunarg-vulkan-${distro_code_name}.list \
            --output /etc/apt/sources.list.d/lunarg-vulkan-${distro_code_name}.list
            sudo apt update
            sudo apt install -y vulkan-sdk shaderc glslang-dev
        fi
    fi

    if [ -d "/usr/include/vulkan" ] && [ -d "/usr/include/shaderc" ]; then
        if ! grep -q VULKAN_SDK ~/.bashrc; then
            echo 'export VULKAN_SDK="/usr"' >> ~/.bashrc
        fi
        export VULKAN_SDK="/usr"
        found_vulkan=true
    fi

    if ! $found_vulkan; then
        curl --silent --show-error --fail -O https://sdk.lunarg.com/sdk/download/latest/linux/vulkan-sdk.tar.gz
        mkdir -p VulkanSDK
        tar -xzf vulkan-sdk.tar.gz -C VulkanSDK
        VK_LAYER_PATH=""
        source "VulkanSDK/$(ls VulkanSDK)/setup-env.sh"

        # Fix pkgconfig file.
        shaderc_pkgconfig_file="VulkanSDK/$(ls VulkanSDK)/$os_arch/lib/pkgconfig/shaderc.pc"
        prefix_path=$(realpath "VulkanSDK/$(ls VulkanSDK)/$os_arch")
        sed -i '3s;.*;prefix=\"'$prefix_path'\";' "$shaderc_pkgconfig_file"
        sed -i '5s;.*;libdir=${prefix}/lib;' "$shaderc_pkgconfig_file"
        export PKG_CONFIG_PATH="$(realpath "VulkanSDK/$(ls VulkanSDK)/$os_arch/lib/pkgconfig")"
        found_vulkan=true
    fi

    if ! $found_vulkan; then
        echo "The environment variable VULKAN_SDK is not set but is required in the installation process."
        echo "Please refer to https://vulkan.lunarg.com/sdk/home#linux for instructions on how to install the Vulkan SDK."
        exit 1
    fi
fi

params_sgl=()

if $custom_glslang; then
    if [ ! -d "./glslang" ]; then
        echo "------------------------"
        echo "  downloading glslang   "
        echo "------------------------"
        # Make sure we have no leftovers from a failed build attempt.
        if [ -d "./glslang-src" ]; then
            rm -rf "./glslang-src"
        fi
        git clone https://github.com/KhronosGroup/glslang.git glslang-src
        pushd glslang-src >/dev/null
        ./update_glslang_sources.py
        mkdir build
        pushd build >/dev/null
        cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${PROJECTPATH}/third_party/glslang" ..
        make -j $(nproc)
        make install
        popd >/dev/null
        popd >/dev/null
    fi
    params_sgl+=(-Dglslang_DIR="${PROJECTPATH}/third_party/glslang" -DUSE_SHADERC=Off)
fi

if [ ! -d "./sgl" ]; then
    echo "------------------------"
    echo "     fetching sgl       "
    echo "------------------------"
    git clone --depth 1 https://github.com/chrismile/sgl.git
fi

if [ -f "./sgl/$build_dir/CMakeCache.txt" ]; then
    if grep -q vcpkg_installed "./sgl/$build_dir/CMakeCache.txt"; then
        echo "Removing old sgl build cache..."
        if [ -d "./sgl/$build_dir_debug" ]; then
            rm -rf "./sgl/$build_dir_debug"
        fi
        if [ -d "./sgl/$build_dir_release" ]; then
            rm -rf "./sgl/$build_dir_release"
        fi
        if [ -d "./sgl/install" ]; then
            rm -rf "./sgl/install"
        fi
    fi
fi

if [ ! -d "./sgl/install" ]; then
    echo "------------------------"
    echo "     building sgl       "
    echo "------------------------"

    pushd "./sgl" >/dev/null
    mkdir -p $build_dir_debug
    mkdir -p $build_dir_release

    pushd "$build_dir_debug" >/dev/null
    cmake .. \
         -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_INSTALL_PREFIX="../install" \
         "${params_sgl[@]}"
    make -j $(nproc)
    make install
    popd >/dev/null

    pushd $build_dir_release >/dev/null
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="../install" \
        "${params_sgl[@]}"
    make -j $(nproc)
    make install
    popd >/dev/null

    popd >/dev/null
fi

params=()

embree_version="3.13.3"
if ! $is_embree_installed && [ $os_arch = "x86_64" ]; then
    if [ ! -d "./embree-${embree_version}.x86_64.linux" ]; then
        echo "------------------------"
        echo "   downloading Embree   "
        echo "------------------------"
        wget "https://github.com/embree/embree/releases/download/v${embree_version}/embree-${embree_version}.x86_64.linux.tar.gz"
        tar -xvzf "embree-${embree_version}.x86_64.linux.tar.gz"
    fi
    params+=(-Dembree_DIR="${PROJECTPATH}/third_party/embree-${embree_version}.x86_64.linux/lib/cmake/embree-${embree_version}")
fi

ospray_version="2.9.0"
if ! $is_ospray_installed && [ $os_arch = "x86_64" ]; then
    if [ ! -d "./ospray-${ospray_version}.x86_64.linux" ]; then
        echo "------------------------"
        echo "   downloading OSPRay   "
        echo "------------------------"
        wget "https://github.com/ospray/OSPRay/releases/download/v${ospray_version}/ospray-${ospray_version}.x86_64.linux.tar.gz"
        tar -xvzf "ospray-${ospray_version}.x86_64.linux.tar.gz"
    fi
    params+=(-Dospray_DIR="${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/cmake/ospray-${ospray_version}")
fi

popd >/dev/null # back to project root

if [ $debug = true ]; then
    echo "------------------------"
    echo "  building in debug     "
    echo "------------------------"
else
    echo "------------------------"
    echo "  building in release   "
    echo "------------------------"
fi

if [ -f "./$build_dir/CMakeCache.txt" ]; then
    if grep -q vcpkg_installed "./$build_dir/CMakeCache.txt"; then
        echo "Removing old application build cache..."
        if [ -d "./$build_dir_debug" ]; then
            rm -rf "./$build_dir_debug"
        fi
        if [ -d "./$build_dir_release" ]; then
            rm -rf "./$build_dir_release"
        fi
        if [ -d "./$destination_dir" ]; then
            rm -rf "./$destination_dir"
        fi
    fi
fi

mkdir -p $build_dir

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
make -j $(nproc)
popd >/dev/null

echo "------------------------"
echo "   copying new files    "
echo "------------------------"
mkdir -p $destination_dir/bin

# Copy the application to the destination directory.
rsync -a "$build_dir/LineVis" "$destination_dir/bin"

# Copy all dependencies of the application to the destination directory.
ldd_output="$(ldd $build_dir/LineVis)"
if ! $is_ospray_installed && [ $os_arch = "x86_64" ]; then
    libembree3_so="$(readlink -f "${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/libembree3.so")"
    libospray_module_cpu_so="$(readlink -f "${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/libospray_module_cpu.so")"
    libopenvkl_so="$(readlink -f "${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/libopenvkl.so")"
    libopenvkl_module_cpu_device_so="$(readlink -f "${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/libopenvkl_module_cpu_device.so")"
    libopenvkl_module_cpu_device_4_so="$(readlink -f "${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/libopenvkl_module_cpu_device_4.so")"
    libopenvkl_module_cpu_device_8_so="$(readlink -f "${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/libopenvkl_module_cpu_device_8.so")"
    libopenvkl_module_cpu_device_16_so="$(readlink -f "${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib/libopenvkl_module_cpu_device_16.so")"
    ldd_output="$ldd_output $libembree3_so $libospray_module_cpu_so $libopenvkl_so $libopenvkl_module_cpu_device_so"
    ldd_output="$ldd_output $libopenvkl_module_cpu_device_4_so $libopenvkl_module_cpu_device_8_so $libopenvkl_module_cpu_device_16_so"
fi
library_blacklist=(
    "libOpenGL" "libGLdispatch" "libGL.so" "libGLX.so"
    "libwayland" "libffi." "libX" "libxcb" "libxkbcommon"
    "ld-linux" "libdl." "libutil." "libm." "libc." "libpthread." "libbsd."
)
for library in $ldd_output
do
    if [[ $library != "/"* ]]; then
        continue
    fi
    is_blacklisted=false
    for blacklisted_library in ${library_blacklist[@]}; do
        if [[ "$library" == *"$blacklisted_library"* ]]; then
            is_blacklisted=true
            break
        fi
    done
    if [ $is_blacklisted = true ]; then
        continue
    fi
    # TODO: Add blacklist entries for pulseaudio and dependencies.
    #cp "$library" "$destination_dir/bin"
    #patchelf --set-rpath '$ORIGIN' "$destination_dir/bin/$(basename "$library")"
done
patchelf --set-rpath '$ORIGIN' "$destination_dir/bin/LineVis"
if ! $is_ospray_installed; then
    ln -sf "./$(basename "$libembree3_so")" "$destination_dir/bin/libembree3.so"
    ln -sf "./$(basename "$libospray_module_cpu_so")" "$destination_dir/bin/libospray_module_cpu.so"
    ln -sf "./$(basename "$libopenvkl_so")" "$destination_dir/bin/libopenvkl.so"
    ln -sf "./$(basename "$libopenvkl_so")" "$destination_dir/bin/libopenvkl.so.1"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_so")" "$destination_dir/bin/libopenvkl_module_cpu_device.so"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_so")" "$destination_dir/bin/libopenvkl_module_cpu_device.so.1"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_4_so")" "$destination_dir/bin/libopenvkl_module_cpu_device_4.so"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_4_so")" "$destination_dir/bin/libopenvkl_module_cpu_device_4.so.1"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_8_so")" "$destination_dir/bin/libopenvkl_module_cpu_device_8.so"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_8_so")" "$destination_dir/bin/libopenvkl_module_cpu_device_8.so.1"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_16_so")" "$destination_dir/bin/libopenvkl_module_cpu_device_16.so"
    ln -sf "./$(basename "$libopenvkl_module_cpu_device_16_so")" "$destination_dir/bin/libopenvkl_module_cpu_device_16.so.1"
fi

# Copy python3 to the destination directory.
# TODO

# Copy the docs to the destination directory.
cp "README.md" "$destination_dir"
if [ ! -d "$destination_dir/LICENSE" ]; then
    mkdir -p "$destination_dir/LICENSE"
    cp -r "docs/license-libraries/." "$destination_dir/LICENSE/"
    cp -r "LICENSE" "$destination_dir/LICENSE/LICENSE-linevis.txt"
    cp -r "submodules/IsosurfaceCpp/LICENSE" "$destination_dir/LICENSE/graphics/LICENSE-isosurfacecpp.txt"
fi
if [ ! -d "$destination_dir/docs" ]; then
    cp -r "docs" "$destination_dir"
fi

# Create a run script.
printf "#!/bin/bash\npushd \"\$(dirname \"\$0\")/bin\" >/dev/null\n./LineVis\npopd\n" > "$destination_dir/run.sh"
chmod +x "$destination_dir/run.sh"


# Run the program as the last step.
echo ""
echo "All done!"
pushd $build_dir >/dev/null

if [[ -z "${LD_LIBRARY_PATH+x}" ]]; then
    export LD_LIBRARY_PATH="${PROJECTPATH}/third_party/sgl/install/lib"
elif [[ ! "${LD_LIBRARY_PATH}" == *"${PROJECTPATH}/third_party/sgl/install/lib"* ]]; then
    export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${PROJECTPATH}/third_party/sgl/install/lib"
fi
if ! $is_ospray_installed && [ $os_arch = "x86_64" ]; then
    export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${PROJECTPATH}/third_party/ospray-${ospray_version}.x86_64.linux/lib"
fi
./LineVis
