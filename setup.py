# BSD 2-Clause License
#
# Copyright (c) 2024, Christoph Neuhauser
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

import sys
import os
import stat
import platform
import glob
import shutil
import subprocess
import urllib
import zipfile
import tarfile
import inspect
from pathlib import Path
from urllib.request import urlopen
import setuptools
from setuptools import setup, find_packages
from setuptools.command.egg_info import egg_info
from setuptools.dist import Distribution
from setuptools.command import bdist_egg
import torch
from torch.utils.cpp_extension import include_paths, library_paths, BuildExtension, \
    IS_WINDOWS, IS_HIP_EXTENSION, ROCM_VERSION, ROCM_HOME, CUDA_HOME
try:
    from torch.utils.cpp_extension import SYCL_HOME
except ImportError:
    SYCL_HOME = None

setup(
        name='test',
        author='Christoph Neuhauser',
        ext_modules=[
        ],
        cmdclass={},
        license_files=('LICENSE',),
)

