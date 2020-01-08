#!/bin/bash
# USAGE:
# ./build-docker.sh [--no-cache]
# builds docker image from scratch (with --no-cache passed) or from latest build increment
docker build --network=host \
  $1 \
  -t av:rovio-nvidia-opengl \
  .
