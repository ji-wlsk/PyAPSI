# docker/py.Dockerfile
ARG PYTHON_VERSION=3.10.4
FROM python:${PYTHON_VERSION}-slim-bullseye AS builder

RUN apt-get update -qq \
 && apt-get install -qq -y \
      build-essential cmake patchelf libtclap-dev python3-dev zip unzip \
 && rm -rf /var/lib/apt/lists/*

COPY --from=pyapsi:base /tmp/vcpkg      /tmp/vcpkg
COPY --from=pyapsi:base /usr/local     /usr/local
COPY --from=pyapsi:base /tmp/apsi/cmake /tmp/apsi/cmake


WORKDIR /src
COPY . .

RUN pip install poetry \
 && poetry config virtualenvs.create false \
 && poetry install --no-interaction --no-ansi
ENV CMAKE_TOOLCHAIN_FILE=/tmp/vcpkg/scripts/buildsystems/vcpkg.cmake  
ENV CMAKE_PREFIX_PATH=/usr/local:/tmp/vcpkg/installed/x64-linux:/tmp/apsi/cmake  
ENV PYBIND11_DIR=/usr/local/lib/python3.10/site-packages/pybind11/share/cmake/pybind11

RUN pip install pybind11 setuptools wheel \
 && mkdir build && cd build \
 && cmake .. \
      -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} \
      -Dpybind11_DIR="${PYBIND11_DIR}" \
      -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
      -DPYTHON_EXECUTABLE=$(which python) \
      -DCMAKE_BUILD_TYPE=Release \
 && cmake --build . --parallel \
 && cp _pyapsi*.so /src/apsi/_pyapsi.so

RUN pip install wheel auditwheel \
 && python setup.py bdist_wheel --dist-dir /tmp/wheelhouse \
 && auditwheel repair /tmp/wheelhouse/apsi-*.whl \
      --plat manylinux_2_31_x86_64 \
      -w /tmp/wheelhouse
