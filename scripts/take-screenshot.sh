#!/bin/bash

# This script takes a screenshot to a given folder,
# filename YYYY-MM/YYYY-MM-DD_HH-mm-SS_UUID.png,
# and copies it to the clipboard using xclip.
#
# Usage: take-screenshot.sh /path/to/dir

uuidgen()
{
	cat "/proc/sys/kernel/random/uuid"
}

SCREENSHOT_DIR="$1"
YMDHMS=$(date '+%Y-%m-%d_%H-%M-%d')
YMD=$(echo "$YMDHMS" | cut -d '_' -f 1)
YM=$(echo "$YMD" | cut -d '-' -f 1-2)
UUID=$(uuidgen)
SCREENSHOT_PATH="${SCREENSHOT_DIR}/${YM}/${YMDHMS}_${UUID}.png"

mkdir -p "$SCREENSHOT_DIR/$YM"

yukino --output "$SCREENSHOT_PATH"

if test ! -f "$SCREENSHOT_PATH"; then
	exit 0
fi

# Copy image to clipboard first
xclip -i -selection clipboard -t image/png < "${SCREENSHOT_PATH}"

# If optipng is available, use it to optimize the png for storage
which optipng >/dev/null 2>&1
if test $? -eq 0; then
	optipng -o7 "$SCREENSHOT_PATH"
fi
