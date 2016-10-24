#!/bin/bash
# 
# Copyright (c) 2015-2016, Gregory M. Kurtzer. All rights reserved.
# 
# “Singularity” Copyright (c) 2016, The Regents of the University of California,
# through Lawrence Berkeley National Laboratory (subject to receipt of any
# required approvals from the U.S. Dept. of Energy).  All rights reserved.
# 
# This software is licensed under a customized 3-clause BSD license.  Please
# consult LICENSE file distributed with the sources of this project regarding
# your rights to use or distribute this software.
# 
# NOTICE.  This Software was developed under funding from the U.S. Department of
# Energy and the U.S. Government consequently retains certain rights. As such,
# the U.S. Government has been granted for itself and others acting on its
# behalf a paid-up, nonexclusive, irrevocable, worldwide license in the Software
# to reproduce, distribute copies to the public, prepare derivative works, and
# perform publicly and display publicly, and to permit other to do so. 
# 
# 

## Basic sanity
if [ -z "$SINGULARITY_libexecdir" ]; then
    echo "Could not identify the Singularity libexecdir."
    exit 1
fi

## Load functions
if [ -f "$SINGULARITY_libexecdir/singularity/functions" ]; then
    . "$SINGULARITY_libexecdir/singularity/functions"
else
    echo "Error loading functions: $SINGULARITY_libexecdir/singularity/functions"
    exit 1
fi

if [ -z "${SINGULARITY_ROOTFS:-}" ]; then
    message ERROR "Singularity root file system not defined\n"
    exit 1
fi

if [ -z "${SINGULARITY_BUILDDEF:-}" ]; then
    exit
fi


########## BEGIN BOOTSTRAP SCRIPT ##########


MIRROR=`singularity_key_get "MirrorURL" "$SINGULARITY_BUILDDEF"`
if [ -z "${MIRROR:-}" ]; then
    MIRROR="https://www.busybox.net/downloads/binaries/busybox-x86_64"
fi


mkdir -p -m 0755 "$SINGULARITY_ROOTFS/bin"
mkdir -p -m 0755 "$SINGULARITY_ROOTFS/etc"

echo "root:!:0:0:root:/root:/bin/sh" > "$SINGULARITY_ROOTFS/etc/passwd"
echo " root:x:0:" > "$SINGULARITY_ROOTFS/etc/group"
echo "127.0.0.1   localhost localhost.localdomain localhost4 localhost4.localdomain4" > "$SINGULARITY_ROOTFS/etc/hosts"

curl "$MIRROR" > "$SINGULARITY_ROOTFS/bin/busybox"

chmod 0755 "$SINGULARITY_ROOTFS/bin/busybox"

eval "$SINGULARITY_ROOTFS/bin/busybox" --install "$SINGULARITY_ROOTFS/bin/"


# If we got here, exit...
exit 0