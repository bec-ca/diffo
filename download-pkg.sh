#!/bin/bash -eu

pkg_name=$1
url=$2

echo "Downloading $pkg_name..."

repo_dir=$PWD

pkgs=$repo_dir/build/external-packages
mkdir -p $pkgs

tmp_dir=$(mktemp -d)
cd $tmp_dir

curl -L -s --show-error -o $pkg_name.tar.gz $url
tar -xf $pkg_name.tar.gz

rm -rf $pkgs/$pkg_name
mv */$pkg_name $pkgs/$pkg_name

