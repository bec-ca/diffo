#!/bin/bash -eu

pkg_name=$1
url=$2

repo_dir=$PWD

pkgs=$repo_dir/build/external-packages
mkdir -p $pkgs

tmp_dir=$(mktemp -d)
pushd $tmp_dir

curl -L -o $pkg_name.tar.gz $url
tar -xf $pkg_name.tar.gz

rm -rf $pkgs/$pkg_name
mv */$pkg_name $pkgs/$pkg_name

