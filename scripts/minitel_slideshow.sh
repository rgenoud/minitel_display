#!/bin/sh

# This small script set up the minitel serial line,
# and for each image in the photo directory,
# resizes it to a 80x72 image in a temp directory, then displays it.
# This way, the photo directory can be updated on the fly.

usage() {
	printf "Usage:\n"
	printf "$0 serial_device photo_directory fast|slow\n\n"
	printf "\tserial_device:     on which serial device the minitel is plugged\n"
	printf "\tphoto_directory:   directory to read the photos from\n"
	printf "\tslow:              standard minitel speed (1200bauds)\n"
	printf "\tfast:              turbo speed \\o/ (4800bauds!!!)\n"
	printf "Example: $0 /dev/ttyS2 /photos/upload fast\n"
}

if [ $# -ne 3 ]; then
	usage
	exit 1
fi

if [ "$3" != "fast" ] && [ "$3" != "slow" ]; then
	usage
	exit 1
fi

TTYS="$1"
PHOTO_DIR="$2"
SPEED_OPT=""
if [ "$3" = "fast" ]; then
	SPEED_OPT="fast"
fi

if [ ! -c "$TTY" ]; then
	printf "$TTY should be a char device\n"
	exit 1
fi

if [ ! -w "$TTY" ]; then
	printf "$TTY should be writable\n"
	exit 1
fi

if [ ! -d "$PHOTO_DIR" ] || [ ! -x "$PHOTO_DIR" ] || [ ! -r "$PHOTO_DIR" ]; then
	printf "$PHOTO_DIR should be a readable directory\n"
	exit 1
fi

# minitel configuration
BAUDRATE=1200
PARITY="cs7 parenb -parodd -inpck -istrip -ignpar ixon ixoff -ixany"
IO="opost onlcr icrnl -iexten ignbrk"
CTRL="brkint hupcl "
STOP="-cstopb"
HANDSHAKING="-crtscts"
TTYS_OPTS="$PARITY $IO $CTRL $STOP $HANDSHAKING"
# this is a working configuration:
# speed 1200 baud; rows 0; columns 0; line = 0;
# intr = ^C; quit = ^\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>;
# eol2 = <undef>; swtch = <undef>; start = ^Q; stop = ^S; susp = ^Z;
# rprnt = ^R; werase = ^W; lnext = ^V; flush = ^O; min = 1; time = 5;
# parenb -parodd cs7 hupcl -cstopb cread clocal -crtscts
# ignbrk -brkint -ignpar -parmrk -inpck -istrip -inlcr -igncr -icrnl
# ixon ixoff -iuclc -ixany -imaxbel -iutf8 -opost -olcuc -ocrnl -onlcr -onocr
# -onlret -ofill -ofdel nl0 cr0 tab0 bs0 vt0 ff0 -isig -icanon -iexten -echo
# -echoe -echok -echonl -noflsh -xcase -tostop -echoprt -echoctl -echoke

stty -F $TTYS raw -onlcr -echo -echoe -echok -echoctl -echoke hupcl
stty -F $TTYS ispeed $BAUDRATE ospeed $BAUDRATE
stty -F $TTYS $TTYS_OPTS

# OUTDIR should be a tmpfs
OUTDIR=/photos_resized
if [ ! -e "$OUTDIR" ]; then
	mkdir -p "$OUTDIR"
	mount -ttmpfs none "$OUTDIR"
	if [ $? -ne 0 ]; then
		echo "Unable to mount a tmpfs in $OUTDIR."
	fi
fi

# resize time is enough, at least on a cubieboard.
DELAY=0
cd "$PHOTO_DIR"
while [ true ]; do
	for i in * ; do
		echo $i
		resize_to_minitel.sh $i $OUTDIR/$i
		minitel_display_nb $TTYS $OUTDIR/$i $SPEED_OPT
		sleep $DELAY
	done
	# everything is removed in the tmpdir
	# this way, photos can be added/removed from the upload dir
	rm $OUTDIR/*
done
