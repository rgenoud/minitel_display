#!/bin/sh

# This script resizes a image to 80x72 (minitel videotext resolution) in gray scale.
# The aspect ratio is kept, but the image is cropped to 80x72

usage() {
	echo "Usage $0 image_to_resize image_resized"
}

if [ $# -ne 2 ]; then
	usage
	exit 1
fi

convert $1 -resize 80x72^ -gravity center -extent 80x72 \
	-set colorspace Gray -separate -average -equalize $2
