# docker/base.Dockerfile
FROM debian:bullseye

RUN apt-get update -q \
 && apt-get install -q -y \
      build-essential \
      git \
      cmake \
      curl \
      autoconf \
      automake \
      pkg-config \
      zip \
      unzip \
 && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg /tmp/vcpkg
RUN /tmp/vcpkg/bootstrap-vcpkg.sh

RUN /tmp/vcpkg/vcpkg install \
      seal[no-throw-tran] \
      kuku \
      log4cplus \
      cppzmq \
      flatbuffers \
      jsoncpp

RUN git clone https://github.com/microsoft/apsi /tmp/apsi
RUN sed -i "s/-D_AVX2_/-D_AVX_/g" /tmp/apsi/CMakeLists.txt
RUN sed -i "s/_AVX2.S/.S/g" /tmp/apsi/common/apsi/fourq/amd64/CMakeLists.txt

WORKDIR /tmp/apsi/build
RUN cmake .. \
      -DCMAKE_TOOLCHAIN_FILE=/tmp/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local
RUN make -j$(nproc)
RUN make install

WORKDIR /
