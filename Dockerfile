### Last update: 20201105
FROM ubuntu:latest

# Update and pull down build tools
RUN \
  apt-get update && \
  apt-get -y upgrade && \
  apt-get install -y apt-utils autoconf bison build-essential byobu curl default-jre emacs \
    fftw3-dev flex gdb gcc gfortran git git-lfs htop libblas-dev \
    libcfitsio-dev libfmt-dev libgtest-dev libhdf5-dev liblapack-dev libncurses-dev \
    libprotobuf-dev libreadline-dev libssl-dev libstarlink-ast-dev libtbb-dev libtool libxml2-dev \
    libzstd-dev libgsl-dev man pkg-config protobuf-compiler python-pip python3-pip \
    software-properties-common unzip vim wcslib-dev wget
    
# Install latest cmake (>= 3.13 required to build gRPC correctly)
RUN \
  wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | apt-key add - && \
  apt-add-repository "deb https://apt.kitware.com/ubuntu/ `lsb_release -cs` main" && \
  apt-get update && \
  apt-get -y install cmake

# Install googletest
RUN \
  cd /usr/src/gtest && \
  cmake . && make && \
  cp *.a /usr/lib

# Get carta dependencies
# casacore data from Kernsuite PPA
RUN \
  apt-add-repository -y -s ppa:kernsuite/kern-5 && \
  apt-get update && \
  apt-get -y install casacore-data

# casacore dependency libsofa not currently necessary
#RUN \
#  pip install numpy && \
#  pip3 install numpy && \
#  wget http://www.iausofa.org/2018_0130_F/sofa_f-20180130.tar.gz && \
#  tar xzf sofa_f-20180130.tar.gz && rm sofa_f-20180130.tar.gz && \
#  cd sofa/20180130/f77/src && make && cp libsofa.a /usr/lib/libsofa.a && \
#  cd /root && rm -rf sofa

# carta-casacore from cartavis PPA
RUN \
  add-apt-repository -y ppa:cartavis/carta-casacore && \
  apt-get update && \
  apt-get install carta-casacore 

# uWS from cartavis PPA
RUN \
  add-apt-repository -y ppa:cartavis/uws && \
  apt-get update && \
  apt-get install uws

# zfp from cartavis ppa
RUN \
  add-apt-repository -y ppa:cartavis/zfp && \
  apt-get update && \
  apt-get install zfp

# gRPC
RUN \
  cd /root && \
  git clone --recurse-submodules https://github.com/grpc/grpc && \ 
  mkdir -p grpc/cmake/build && cd grpc/cmake/build && \
  cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local -DgRPC_SSL_PROVIDER=package ../.. && \
  make && make install && \
  cd /root && rm -rf grpc

# Build carta-backend (currently checkouts the confluence/generic-scripting branch)
RUN \
  apt-get -y install libxml2-dev && \
  git clone https://github.com/CARTAvis/carta-backend.git && \
  cd carta-backend && \
  git checkout confluence/generic-scripting && \
  git submodule init && git submodule update && \
  mkdir build && cd build && \
  cmake .. -DCMAKE_CXX_FLAGS="-I/usr/include/casacode -I/usr/include/casacore" -DCMAKE_CXX_STANDARD_LIBRARIES="-L/usr/local/lib -lcasa_imageanalysis" && \
  make

# Forward port so that the webapp can properly access it
# from outside of the container
EXPOSE 3002
# Do the same with the gRPC service port
EXPOSE 50051

ENV HOME /root
# Required for running the backend
ENV LD_LIBRARY_PATH /usr/local/lib
ENV CASAPATH "/usr/share/casacore linux local `hostname`"

WORKDIR /root

# overwrite this with 'CMD []' in a dependent Dockerfile
CMD ["bash"]

