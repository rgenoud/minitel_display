/*
 * Minitel Photo Display
 *
 * Copyright (C) 2014,2015 Richard Genoud
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <stdbool.h>
#include <wand/MagickWand.h>

#define NB_SHADES 8

#define MINITEL_WIDTH 40
#define MINITEL_HEIGHT 24

#define IMG_WIDTH (MINITEL_WIDTH * 2)
#define IMG_HEIGHT (MINITEL_HEIGHT * 3)

#define CMD_SZ 5

#define LOG_ERR(format, args...) \
	fprintf(stderr, format "%s", args, "\n")

#ifdef DEBUG
#define LOG_DEBUG_SAME_LINE(format, args...) \
	fprintf(stderr, format, args)

#define LOG_DEBUG_NEWLINE() \
	LOG_DEBUG_SAME_LINE("%s", "\n")

#define LOG_DEBUG(format, args...) \
	LOG_DEBUG_SAME_LINE(format "%s", args, "\n")
#else
#define LOG_DEBUG_SAME_LINE(format, args...)
#define LOG_DEBUG_NEWLINE()
#define LOG_DEBUG(format, args...)
#endif

#define __maybe_unused __attribute__ ((unused))

static void ThrowWandException(MagickWand *wand)
{
	char *description;
	ExceptionType severity;

	description = MagickGetException(wand, &severity);
	/* format is %s%s%lu for GetMagickModule() alone */
	(void) LOG_ERR("%s %s %lu %s", GetMagickModule(), description);
	description = (char *) MagickRelinquishMemory(description);
	exit(-1);
}

static void init_minitel(int fd)
{
	size_t nb;
	uint8_t cmd[CMD_SZ];

	nb = 0;
	cmd[nb++] = 0x0C; /* clear page */
	(void) write(fd, cmd, nb);
	(void) tcdrain(fd);
	nb = 0;
	cmd[nb++] = 0x0E; /* switch to graphic mode */
	(void) write(fd, cmd, nb);
	(void) tcdrain(fd);
}

static int set_minitel_speed(int fd, int speed)
{
	size_t nb;
	uint8_t cmd[CMD_SZ];
	struct termios tty;

	memset (&tty, 0, sizeof tty);

	if (tcgetattr(fd, &tty))
	{
		LOG_ERR("%s", "tcgetattr failed");
		return -1;
	}

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	nb = 0;
	/* speed selection */
	cmd[nb++] = 0x1B;
	cmd[nb++] = 0x3A;
	cmd[nb++] = 0x6B;
	switch (speed) {
	case B300: cmd[nb++] = 0x52; break;
	case B1200: cmd[nb++] = 0x64; break;
	case B4800: cmd[nb++] = 0x76; break;
	case B9600: cmd[nb++] = 0x7F; break;
	default: LOG_ERR("unsupported speed %d", speed); return -1;
	}
	(void) write(fd, cmd, nb);
	(void) tcdrain(fd);
	/*
	 * If we don't wait, the command is not understood...
	 */
	usleep(500000);

	if (tcsetattr(fd, TCSADRAIN, &tty) != 0)
	{
		LOG_ERR("%s", "tcgetattr failed");
		return -1;
	}
	return 0;
}

static int open_minitel(char *dev, bool fast)
{
	int fd;
	int err = -1;

	fd = open(dev, O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0) {
		LOG_ERR("error opening %s", dev);
		goto out;
	}
	init_minitel(fd);
	err = set_minitel_speed(fd, fast ? B4800 : B1200);
out:
	if (err) {
		close(fd);
		fd = -1;
	}
	return fd;
}

static uint8_t *minitel_bmp(uint8_t *buf, unsigned long pos_x,
			    unsigned long pos_y,
			    uint8_t *bmp, size_t w)
{
	uint8_t shades[NB_SHADES];
	uint8_t cmd = 1 << 5;
	static unsigned long current_x = 0;
	static unsigned long current_y = 1;
	static int current_foreground = -1;
	static int current_background = -1;
	int foreground = -1;
	int background = -1;
	unsigned long current_xy, pos_xy;

	memset(shades, 0, NB_SHADES);

	/*
	 * We can only paint the 6 pixels with 2 colors,
	 * so we have to find the 2 most present colours
	 */
	shades[*(bmp + 0 + 0 * w)]++;
	shades[*(bmp + 1 + 0 * w)]++;
	shades[*(bmp + 0 + 1 * w)]++;
	shades[*(bmp + 1 + 1 * w)]++;
	shades[*(bmp + 0 + 2 * w)]++;
	shades[*(bmp + 1 + 2 * w)]++;

	for (int i = 0, max = -1; i < NB_SHADES; i++) {
		if (shades[i] > max) {
			max = shades[i];
			foreground = i;
		}
	}

	for (int i = 0, max = -1; i < NB_SHADES; i++) {
		if (foreground == i) {
			continue;
		}
		if (shades[i] > max) {
			max = shades[i];
			background = i;
		}
	}
	/*
	 * Let's always have the darker color for
	 * background
	 */
	if (foreground < background) {
		int tmp = background;
		background = foreground;
		foreground = tmp;
	}

	/*
	 * If all the pixels are black, there's no need painting them
	 */
	if (shades[0] == 6) {
		return buf;
	}

	/*
	 * check if we need to jump to a new location
	 */
	current_xy = (current_y - 1) * MINITEL_WIDTH + current_x;
	pos_xy = (pos_y - 1) * MINITEL_WIDTH + pos_x;

	/*
	 * a jump is quite expensive:
	 * - 3 chars
	 * - switch back to graphic
	 * - reset {fore,back}ground
	 * => 8 chars !!
	 * So we don't jump for less than 8 chars.
	 */
	if ((pos_xy - current_xy) > 8) {
		LOG_DEBUG("jump from %lux%lu to %lux%lu\n",
			  current_x, current_y, pos_x, pos_y);
		*buf++ = 0x1F; /* jump */
		*buf++ = pos_y + 0x40;
		*buf++ = pos_x + 0x40;
		*buf++ = 0x0E;
		current_background = current_foreground = -1;
	} else {
		if ((pos_xy - current_xy) > 1) {
			for (unsigned long i = 0; i < (pos_xy - current_xy - 1); i++) {
				*buf++ = 0x09; /* right arrow */
			}
		}
	}

	/*
	 * We are using the foreground color for the matching and
	 * lighter colors.
	 * It seems to be a little better than with only the maching one.
	 */
	if (*(bmp + 0 + 0 * w) >= foreground) cmd |= 1 << 0;
	if (*(bmp + 1 + 0 * w) >= foreground) cmd |= 1 << 1;
	if (*(bmp + 0 + 1 * w) >= foreground) cmd |= 1 << 2;
	if (*(bmp + 1 + 1 * w) >= foreground) cmd |= 1 << 3;
	if (*(bmp + 0 + 2 * w) >= foreground) cmd |= 1 << 4;
	if (*(bmp + 1 + 2 * w) >= foreground) cmd |= 1 << 6;
	/*
	 * Color codes, from darker to lighter
	 * "\x1B\x40"
	 * "\x1B\x44"
	 * "\x1B\x41"
	 * "\x1B\x45"
	 * "\x1B\x42"
	 * "\x1B\x46"
	 * "\x1B\x43"
	 * "\x1B\x47"
	 */

	if (current_foreground != foreground) {
		*buf++ = 0x1B;
		switch (foreground) {
		case 0: *buf++ = 0x40; break;
		case 1: *buf++ = 0x44; break;
		case 2: *buf++ = 0x41; break;
		case 3: *buf++ = 0x45; break;
		case 4: *buf++ = 0x42; break;
		case 5: *buf++ = 0x46; break;
		case 6: *buf++ = 0x43; break;
		case 7: *buf++ = 0x47; break;
		}
		current_foreground = foreground;
	}

	if (current_background != background) {
		*buf++ = 0x1B;
		switch (background) {
		case 0: *buf++ = 0x50; break;
		case 1: *buf++ = 0x54; break;
		case 2: *buf++ = 0x51; break;
		case 3: *buf++ = 0x55; break;
		case 4: *buf++ = 0x52; break;
		case 5: *buf++ = 0x56; break;
		case 6: *buf++ = 0x53; break;
		case 7: *buf++ = 0x57; break;
		}
		current_background = background;
	}

	*buf = cmd;
	buf++;
	current_y = pos_y;
	current_x = pos_x;

	return buf;
}

static uint8_t *read_image(const char *img_path, size_t *height, size_t *width)
{
	MagickWand *image_wand = NULL;
	MagickBooleanType status;
	MagickPixelPacket pixel;
	PixelIterator *iterator;
	PixelWand **pixels;
	uint8_t *bitmap = NULL;
	long y;
	register long x;
	int err = -1;

	/* Read an image. */
	MagickWandGenesis();
	image_wand = NewMagickWand();
	status = MagickReadImage(image_wand, img_path);

	if (status == MagickFalse) {
		ThrowWandException(image_wand);
		goto out;
	}

	/* Sigmoidal non-linearity contrast control. */
	iterator = NewPixelIterator(image_wand);
	if (iterator == (PixelIterator *) NULL) {
		ThrowWandException(image_wand);
		goto out;
	}

	*height = MagickGetImageHeight(image_wand);
	*width = MagickGetImageWidth(image_wand);

	if ((*height != IMG_HEIGHT) || (*width != IMG_WIDTH)) {
		LOG_ERR("width x height must be %dx%d, not %zux%zu\n",
			IMG_WIDTH, IMG_HEIGHT, *width, *height);
		goto out;
	}

	bitmap = calloc(*width * *height, 1);

	/*
	 * Get an array of red pixels (as far as it is grayscale,
	 * we don't care if it's red or blue or whatever
	 */
	for (y = 0; y < (long) *height; y++) {
		pixels = PixelGetNextIteratorRow(iterator, width);
		if (pixels == (PixelWand **) NULL) {
			LOG_ERR("%s", "NULL wand !!");
			break;
		}
		for (x = 0; x < (long) *width; x++) {
			PixelGetMagickColor(pixels[x], &pixel);
			*(bitmap + x + (y * *width)) = pixel.red;
		}
	}
	if (y < (long) MagickGetImageHeight(image_wand)) {
		LOG_ERR("%s", "height problem !!");
		ThrowWandException(image_wand);
	}
	err = 0;
out:
	if (err) {
		free(bitmap);
		bitmap = NULL;
	}
	iterator = DestroyPixelIterator(iterator);
	image_wand = DestroyMagickWand(image_wand);

	MagickWandTerminus();

	return bitmap;
}

static void shift_bitmap(uint8_t *bitmap, unsigned long height,
			 unsigned long width, uint8_t shift)
{
	unsigned long y;
	register unsigned long x;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			*(bitmap + x + (y * width)) >>= shift;
		}
	}
}

static void __maybe_unused dump_bitmap(uint8_t __maybe_unused *bitmap,
				       unsigned long height,
				       unsigned long width)
{
	unsigned long y;
	unsigned long x;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			LOG_DEBUG_SAME_LINE("%d", *(bitmap + x + (y * width)));
		}
		LOG_DEBUG_NEWLINE();
	}
	LOG_DEBUG_NEWLINE();
}

int main(int argc,char **argv) {
	unsigned long width;
	uint8_t *bitmap = NULL;
	uint8_t *ptr;
	/* 8:max number of commands for 1 char; 6= initialization */
	uint8_t bmp_cmd[MINITEL_WIDTH * MINITEL_HEIGHT * 8 + 6];
	unsigned long y, pos_y;
	unsigned long x, pos_x;
	unsigned long height;
	bool fast = false;
	int fd = -1;

	if ((argc < 3) || (argc > 4)) {
		(void) LOG_ERR("Usage: %s device image [fast]\n", argv[0]);
		exit(0);
	}

	if ((argc == 4) && !strcmp("fast", argv[3]))
		fast = true;

	/* Read an image into a bitmap grayscale */
	bitmap = read_image(argv[2], (size_t *)&height, (size_t *)&width);
	if (!bitmap) {
		goto out;
	}

	/* open the minitel */
	fd = open_minitel(argv[1], fast);
	if (fd < 0)
		goto out;

	/*
	 * convert into 8 shades of colors
	 */
	shift_bitmap(bitmap, height, width, 5);

	LOG_DEBUG("height=%lu", height);
	LOG_DEBUG("width=%lu", width);

	ptr = bmp_cmd;
	/*
	 * a minitel char is 3 pixel in height and 2 in width,
	 * so we have to loop on each 2x3 pixel
	 */
	for (y = 0, pos_y = 1; y < IMG_HEIGHT; y += 3, pos_y++) {
		LOG_DEBUG("y=%ld len = %ld\n", y, (long int)(ptr - bmp_cmd));
		for (x = 0, pos_x = 1; x < IMG_WIDTH; x += 2, pos_x++) {
			ptr = minitel_bmp(ptr, pos_x, pos_y,
					  bitmap + x + (IMG_WIDTH * y),
					  IMG_WIDTH);
		}
	}

	LOG_DEBUG("len=%ld", (long int)(ptr - bmp_cmd));

	write(fd, bmp_cmd, ptr - bmp_cmd);

	LOG_DEBUG("%s", "End !");

	/*
	 * We have to drain, otherwise the photo is not complete on the minitel.
	 */
	(void) tcdrain(fd);
	LOG_DEBUG("%s", "Drained !");
out:
	set_minitel_speed(fd, B1200);
	if (fd >= 0)
		close(fd);

	LOG_DEBUG("%s", "Closed !");

	free(bitmap);

	return 0;
}
