FROM ubuntu:latest

# Update and pull down build tools
RUN \
  apt-get update && \
  apt-get -y upgrade && \
  apt-get install -y bison build-essential byobu cmake curl default-jre emacs \
    fftw3-dev flex gdb gcc gfortran git git-lfs htop libblas-dev libboost-all-dev \
    libcfitsio-dev libfmt-dev libgtest-dev libhdf5-dev liblapack-dev libncurses-dev \
    libprotobuf-dev libreadline-dev libssl-dev libstarlink-ast-dev libtbb-dev \
    man protobuf-compiler python-pip python3-pip software-properties-common \
    unzip vim wcslib-dev wget

# Install googletest
RUN \
  cd /usr/src/gtest && \
  cmake . && make && \
  cp *.a /usr/lib

# Get carta dependencies
# casacore data
RUN \
  git lfs install && \
  mkdir -p /usr/local/share/casacore && cd /usr/local/share/casacore && \
  git clone --no-checkout https://open-bitbucket.nrao.edu/scm/casa/casa-data.git data && \
  cd data && git show HEAD:distro | bash

# casacore dependencies
RUN \
  pip install numpy && \
  pip3 install numpy && \
  wget http://www.iausofa.org/2018_0130_F/sofa_f-20180130.tar.gz && \
  tar xzf sofa_f-20180130.tar.gz && rm sofa_f-20180130.tar.gz && \
  cd sofa/20180130/f77/src && make && cp libsofa.a /usr/lib/libsofa.a && \
  cd /root && rm -rf sofa

# casacore (add '-j<N>' to 'make' to speed things up)
RUN \
  git clone https://github.com/casacore/casacore.git && \
  mkdir -p casacore/build && cd casacore/build && \
  cmake .. -DUSE_FFTW3=ON -DUSE_HDF5=ON -DUSE_THREADS=ON -DUSE_OPENMP=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
  make && make install && \
  cd /root && rm -rf casacore

# uWS
RUN \
  cd /root && \
  git clone https://github.com/uNetworking/uWebSockets.git && \
  cd uWebSockets && git checkout v0.14 && cd .. && \
  make default install -C uWebSockets && \
  rm -rf uWebSockets

# zfp
RUN \
  cd /root && \
  git clone https://github.com/LLNL/zfp.git && \
  mkdir -p zfp/build && cd zfp/build && \
  cmake .. && make all install && \
  cd /root && rm -rf zfp

# Copy nrao-carta-backend into image (must be in Dockerfile directory)
COPY nrao-carta-backend /root/nrao-carta-backend

# Forward port so that the webapp can properly access it
# from outside of the container
EXPOSE 3002

ENV HOME /root
# Required for running the backend
ENV LD_LIBRARY_PATH /usr/local/lib
ENV CASAPATH "/usr/local/share/casacore linux local `hostname`"

WORKDIR /root

# overwrite this with 'CMD []' in a dependent Dockerfile
CMD ["bash"]
