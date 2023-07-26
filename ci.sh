#!/bin/bash -eu

export MELLOW=build/mellow.bootstrap

function build_bootstrap() {
  echo "Downloading and compiling mellow..."
  bootstrap_dir=.bootstrap
  mellow_dir=$bootstrap_dir/mellow-with-deps

  rm -rf $bootstrap_dir
  mkdir $bootstrap_dir
  pushd $bootstrap_dir
  curl -s -L "https://github.com/bec-ca/mellow/releases/download/v0.0.3/mellow-with-deps-v0.0.3.tar.gz" -o mellow-with-deps.tar.gz
  tar -xf mellow-with-deps.tar.gz
  popd

  pushd $mellow_dir
  make -j $(nproc) -f Makefile.bootstrap
  popd

  mkdir -p build
  cp $mellow_dir/build/bootstrap/mellow/mellow $MELLOW
}

if ! [ -f $MELLOW ]; then
  build_bootstrap
fi

make fetch
PROFILE=release make
