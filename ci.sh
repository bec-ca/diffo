#!/bin/bash -eu

./download-pkg.sh bee https://github.com/bec-ca/bee/archive/refs/tags/v1.1.0.tar.gz
./download-pkg.sh command https://github.com/bec-ca/command/archive/refs/tags/v1.0.1.tar.gz

for file in diffo/*.cpp; do
  echo "Compiling $file..."
  clang++ -c $(cat compile_flags.txt) $file
done
