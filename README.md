[![pipeline status](https://gitlab.inria.fr/bramas/tbfmm/badges/master/pipeline.svg)](https://gitlab.inria.fr/bramas/tbfmm/commits/master)
[![coverage report](https://gitlab.inria.fr/bramas/tbfmm/badges/master/coverage.svg)](https://gitlab.inria.fr/bramas/tbfmm/commits/master)

This project is an FMM code designed to be lightweight and allow
an easy customization of the kernel.

# Compilation

Simply go in the build dir and use cmake as usual:
```
# To enable SPETABARU
git submodule init && git submodule update
# Go in the build dir
mkdir build
cd build
# To enable testing: cmake -DUSE_TESTING=ON -DUSE_SIMU_TESTING=ON ..
cmake ..
# To find FFTW
cmake -DFFTW_ROOT=path-to-fftw ..
# or (FFTW_DIR FFTWDIR)
cmake ..

# Build
make
```


# Running the tests

Use
```
make test
```

To get the output of the tests, use:
```
CTEST_OUTPUT_ON_FAILURE=TRUE make test
```

# Coverage result

Can be found here: https://bramas.gitlabpages.inria.fr/tbfmm/

