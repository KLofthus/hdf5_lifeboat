HDF5 version 1.14.2 released on 2023-08-11

# Experimental

[![Lifeboat hdf5 dev autotools CI](https://github.com/LifeboatLLC/hdf5_lifeboat/actions/workflows/main-auto.yml/badge.svg)](https://github.com/LifeboatLLC/hdf5_lifeboat/actions/workflows/main-auto.yml)
[![Lifeboat hdf5 dev cmake CI](https://github.com/LifeboatLLC/hdf5_lifeboat/actions/workflows/main-cmake.yml/badge.svg)](https://github.com/LifeboatLLC/hdf5_lifeboat/actions/workflows/main-cmake.yml)

This branch contains modified source code of HDF5 r1.14.2. The code will be used to
prototype changes for multi-threaded support before creating PRs to the HDF5 develop
branch. This is for Lifeboat's internal use only but anyone is welcome to watch our
progress.

For autotools builds, one must use the `--enable-multithread` option for configure
to enable the multithread support. For CMake builds, one must use the
`HDF5_ENABLE_MULTITHREAD=ON` option to enable the multithread support.
On Mac OS, this requires the presence of the Pthread library and the Atomic header
(stdatomic.h). On Linux, it requires the presence of the Pthread and Atomic libraries
and the Atomic header.  Missing any of these requirements will cause configuration to
fail. Using the multithread feature requires disabling the high-level API, C++, Fortran,
Java interfaces, and thread safe.

The following commands are examples to enable the multithread support:

## Autotools

    > configure --enable-build-mode=debug --enable-multithread --disable-hl

## CMake

    > cmake -DCMAKE_BUILD_TYPE=Debug -DHDF5_ENABLE_MULTITHREAD=ON -DHDF5_BUILD_HL_LIB=OFF ..

Currently, the only test program to check the correctness of multithread support is
test/mt_id_test.c.  During the build of the library and the test program, there are
multiple warnings related to the atomic issues that we're investigating and fixing.

-------------

HDF5 version 1.14.2 released on 2023-08-11

![HDF5 Logo](doxygen/img/HDF5.png)

[![1.14 build status](https://img.shields.io/github/actions/workflow/status/HDFGroup/hdf5/main.yml?branch=hdf5_1_14&label=1.14)](https://github.com/HDFGroup/hdf5/actions?query=branch%3Ahdf5_1_14)
[![BSD](https://img.shields.io/badge/License-BSD-blue.svg)](https://github.com/HDFGroup/hdf5/blob/develop/COPYING)

*Please refer to the release_docs/INSTALL file for installation instructions.*

This repository contains a high-performance library's source code and a file format
specification that implement the HDF5® data model. The model has been adopted across
many industries and this implementation has become a de facto data management standard
in science, engineering, and research communities worldwide.

The HDF Group is the developer, maintainer, and steward of HDF5 software. Find more
information about The HDF Group, the HDF5 Community, and other HDF5 software projects,
tools, and services at [The HDF Group's website](https://www.hdfgroup.org/). 

DOCUMENTATION
-------------
This release is fully functional for the API described in the documentation.

   https://portal.hdfgroup.org/display/HDF5/The+HDF5+API

Full Documentation and Programming Resources for this release can be found at

   https://portal.hdfgroup.org/display/HDF5

The latest doxygen documentation generated on changes to develop is available at:

   https://hdfgroup.github.io/hdf5/

See the [RELEASE.txt](/release_docs/RELEASE.txt) file in the [release_docs/](/release_docs/) directory for information specific
to the features and updates included in this release of the library.

Several more files are located within the [release_docs/](/release_docs/) directory with specific
details for several common platforms and configurations.

    INSTALL - Start Here. General instructions for compiling and installing the library
    INSTALL_CMAKE  - instructions for building with CMake (Kitware.com)
    INSTALL_parallel - instructions for building and configuring Parallel HDF5
    INSTALL_Windows and INSTALL_Cygwin - MS Windows installations.



HELP AND SUPPORT
----------------
Information regarding Help Desk and Support services is available at

   https://portal.hdfgroup.org/display/support/The+HDF+Help+Desk



FORUM and NEWS
--------------
The [HDF Forum](https://forum.hdfgroup.org) is provided for public announcements and discussions
of interest to the general HDF5 Community.

   - News and Announcements
   https://forum.hdfgroup.org/c/news-and-announcements-from-the-hdf-group

   - HDF5 Topics
   https://forum.hdfgroup.org/c/hdf5

These forums are provided as an open and public service for searching and reading.
Posting requires completing a simple registration and allows one to join in the
conversation.  Please read the [instructions](https://forum.hdfgroup.org/t/quickstart-guide-welcome-to-the-new-hdf-forum
) pertaining to the Forum's use and configuration.



SNAPSHOTS, PREVIOUS RELEASES AND SOURCE CODE
--------------------------------------------
Periodically development code snapshots are provided at the following URL:
    
   https://github.com/HDFGroup/hdf5/releases/tag/snapshot-1.14

Source packages for current and previous releases are located at:
    
   https://portal.hdfgroup.org/display/support/Downloads

Development code is available at our Github location:
    
   https://github.com/HDFGroup/hdf5.git

