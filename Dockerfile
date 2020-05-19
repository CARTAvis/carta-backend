FROM ubuntu:bionic

# Update and pull down build tools
RUN \
  apt-get update && \
  apt-get -y upgrade && \
  apt-get install -y apt-utils autoconf bison build-essential byobu curl default-jre emacs \
    fftw3-dev flex gdb gcc gfortran git git-lfs htop libblas-dev \
    libcfitsio-dev libfmt-dev libgtest-dev libhdf5-dev liblapack-dev libncurses-dev \
    libreadline-dev libssl-dev libstarlink-ast-dev libtbb-dev libtool libxml2-dev \
    libzstd-dev libgsl-dev man pkg-config python-pip python3-pip \
    software-properties-common unzip vim wcslib-dev wget cmake
    
# Get carta dependencies
# casacore data from Kernsuite PPA
RUN \
  apt-add-repository -y -s ppa:kernsuite/kern-5 && \
  apt-get update && \
  apt-get -y install casacore-data

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

# grpc from webispy ppa
RUN \
  add-apt-repository ppa:webispy/grpc && \
  apt-get update && \
  apt-get install -y libprotobuf-dev protobuf-compiler libgrpc++-dev libgrpc-dev protobuf-compiler-grpc googletest

# Build carta-backend (currently checkouts the confluence/generic-scripting branch)
RUN \
  apt-get -y install libxml2-dev && \
  git clone https://github.com/CARTAvis/carta-backend.git && \
  cd carta-backend && \
  git checkout angus/build_test && \
  git submodule init && git submodule update && \
  mkdir build && cd build && \
  cmake .. -DCMAKE_CXX_FLAGS="-I/usr/include/casacode" && \ 
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

