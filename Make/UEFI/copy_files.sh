#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
function usage {
    echo "This script will copy all files required to build opensea-operations to the edk2/UDK path specified."
    echo "How to use: copy_files.sh <path to edk2 directory>"
}

#check that we were given the correct number of parameters
if [ "$#" -lt 1 ]; then
    usage
fi
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
openseaOperationsBaseDir=$(dirname "$(dirname "$scriptDir")")

#check that the directory exists
if [ -d "$1" ]; then
    mkdir -p "$1/opensea-libs/opensea-operations"
    opensealibsDir="$1/opensea-libs"
    openseaoperationsDir="$opensealibsDir/opensea-operations"
    #start by copying the files to build the lib
    cp -r "$openseaOperationsBaseDir/include" "$openseaoperationsDir"
    cp -r "$openseaOperationsBaseDir/src" "$openseaoperationsDir"
    #copy UEFI makefiles
    cp -r "$openseaOperationsBaseDir/Make/UEFI/opensea-operations"* "$openseaoperationsDir"
else
    echo "Cannot find specified path: $1"
fi
