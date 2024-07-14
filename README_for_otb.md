# Postgis for OpenTenBase


## Introduction

This repo is a modified version of postgis extension based on Postgis 3.2.1. This version aims to enable the PostGIS extension to support the distributed features of OpenTenBase and to provide as much support as possible for any issues users may encounter during its use.




## How to install


```shell
SOURCECODE_PATH=/your/path/to/OpenTenBase/source/code
INSTALL_PATH=/your/path/to/install/

# Get source code of Opentenbase


# Get source code of postgis-for-otb
cd ${SOURCE_CODE_PATH}/contrib
git clone https://github.com/dfzhang2022/postgis-for-opentenbase.git
# mv ./postgis-for-opentenbase/ ./postgis-3.2.1/

# Copy deps pkgs & compile 
cp ./postgis-for-opentenbase/deps/*.tar.gz ./
cp ./postgis-for-opentenbase/deps/*.tar.bz2 ./
cp ./postgis-for-opentenbase/deps/*.zip ./
cp ./postgis-for-opentenbase/deps/postgis_compile.sh ./

./postgis_compile.sh #Assuming user already build

# Then postgis extension has already built & install to your OpenTenBase installation dir.

```


## How to Testing
```shell

cd ${SOURCE_CODE_PATH}/contrib/postgis-for-opentenbase/
rm -r /tmp/pgis_reg/* 
make installcheck # This would output result to cmd line.

# If user wants to read from /tmp/pgis_reg/res.txt, redirecting output using this cmd below.
# make installcheck > /tmp/pgis_reg/res.txt 2>&1 

```