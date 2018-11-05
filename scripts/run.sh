#!/bin/bash
  
dirname=`dirname $0`

echo "dirname $dirname"

## source the measures data
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )/../etc"

export CASAPATH="../../../../$DIR linux"

./hdf5_image_viewer --folder $HOME/CARTA/Images

