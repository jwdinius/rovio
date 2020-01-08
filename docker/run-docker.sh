#!/bin/bash
# USAGE:
# ./run-docker.sh {path-to-euroc-dataset}
docker run -it --rm \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  -v $1:/home/rovio/data \
  --name rovio-c \
  --net host \
  --runtime=nvidia \
  av:rovio-nvidia-opengl
