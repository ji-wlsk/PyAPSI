# setup.py

#!/usr/bin/env python3
import os
import re
import subprocess
import sys

from setuptools import Extension, find_packages, setup
from setuptools.command.build_ext import build_ext

__version__ = "0.1.2"


# Windows 플랫폼 매핑(유지)
PLAT_TO_CMAKE = {
    "win32": "Win32",
    "win-amd64": "x64",
    "win-arm32": "ARM",
    "win-arm64": "ARM64",
}


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext: CMakeExtension):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        debug = int(os.environ.get("DEBUG", 0)) if self.debug is None else self.debug
        cfg = "Debug" if debug else "Release"

        # vcpkg 설치 위치 (docker에서 설정한 환경변수)
        vcpkg_root = os.environ.get("VCPKG_ROOT_DIR") or os.environ.get("CMAKE_TOOLCHAIN_FILE", "").rsplit("/scripts/", 1)[0]
        toolchain_file = os.environ.get("CMAKE_TOOLCHAIN_FILE")
        prefix_path = os.environ.get("CMAKE_PREFIX_PATH", "")
        pybind11_dir = os.environ.get("PYBIND11_DIR", "")

        # CMake 인자 모음
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DCMAKE_BUILD_TYPE={cfg}",
        ]

        # (1) vcpkg 툴체인 넘기기
        if toolchain_file:
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}")

        # (2) CMAKE_PREFIX_PATH 에 vcpkg, APSI, pybind11 경로 추가
        if prefix_path:
            cmake_args.append(f"-DCMAKE_PREFIX_PATH={prefix_path}")
        if pybind11_dir:
            # 세미콜론 구분자로 경로 추가
            cmake_args.append(f"-Dpybind11_DIR={pybind11_dir}")

        # Windows MSVC 처리(기존 로직 유지)...
        build_args = []
        if self.compiler.compiler_type != "msvc":
            if not os.environ.get("CMAKE_GENERATOR", "") or os.environ.get("CMAKE_GENERATOR") == "Ninja":
                try:
                    import ninja
                    cmake_args += ["-GNinja", f"-DCMAKE_MAKE_PROGRAM={ninja.BIN_DIR}/ninja"]
                except ImportError:
                    pass
        else:
            # MSVC generator 아키텍처 지정 로직 생략(기존 그대로)

            pass  # (생략)

        # 병렬 빌드 옵션
        if hasattr(self, "parallel") and self.parallel:
            build_args += [f"-j{self.parallel}"]

        build_temp = os.path.join(self.build_temp, ext.name)
        os.makedirs(build_temp, exist_ok=True)

        # CMake configure
        subprocess.check_call(["cmake", ext.sourcedir] + cmake_args, cwd=build_temp)
        # CMake build
        subprocess.check_call(["cmake", "--build", "."] + build_args, cwd=build_temp)


setup(
    name="apsi",
    version=__version__,
    author="Lukas Grossberger",
    author_email="code@grossberger.xyz",
    url="https://github.com/LGro/PyAPSI",
    description="Python wrapper for APSI",
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    packages=find_packages(),
    ext_modules=[CMakeExtension("_pyapsi")],
    cmdclass={"build_ext": CMakeBuild},
    extras_require={"test": "pytest"},
    zip_safe=False,
    python_requires=">=3.8,<3.11",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Programming Language :: Python :: 3.10",
        "Typing :: Typed",
    ],
    install_requires=["pybind11>=2.8.0"],
)