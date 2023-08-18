#!/bin/bash

SCRIPT_PATH=$(dirname $(readlink -f $0))
SRC_DIR="${SCRIPT_PATH}/../"


# Call getopt to validate the provided input. 
options=$(getopt -o v: -- "$@")
[ $? -eq 0 ] || { 
    echo "Incorrect options provided"
    exit 1
}

eval set -- "$options"
while true; do
    case "$1" in
    -v)
        shift; # The arg is next in position args
        VERSION=$1
        ;;
    --)
        shift
        break
        ;;
    esac
    shift
done


if test -z "${VERSION}"; then
    echo "$0 : you must past a version using -v [VERSION] and this version must be tagged"
    exit 1
fi


echo "Trying to pack version $VERSION"


git -C "${SRC_DIR}" archive --format=tar.gz --prefix=tau_metric_proxy-${VERSION}/ ${VERSION} > tau_metric_proxy-${VERSION}.tar.gz