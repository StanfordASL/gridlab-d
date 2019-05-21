# GridLAB-D
This repo holds ASL's fork of [GridLAB-D](https://github.com/gridlab-d/gridlab-d)

The default branch is master-asl. The master branch is intended to track the upstream master branch.

## Dependencies
GridLAB-D depends on autoconf, automake, libtool and xerces-c.


## Build instructions
The build instructions are based on the README files in this repo and those in [SLAC's version of GridLAB-D](https://github.com/dchassin/gridlabd).

### Linux
```shell
cd <repo path>
autoreconf -if
./configure --enable-silent-rules 'CFLAGS=-g -O0 -w' 'CXXFLAGS=-g -O0 -w' 'LDFLAGS=-g -O0 -w'
make
sudo make install
export PATH=$PWD/install/bin:$PATH
```

#### Validate installation
Check GridLAB-D version:
```shell
gridlabd --version
```

Run unit tests:
```shell
gridlabd --validate
```
Note: this takes around 15 minutes. 
Do not get alarmed if not 100% of the tests pass. Typically, around 98% or so do.
