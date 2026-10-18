#!/bin/sh
# Generate short test tones for vlz smoke tests.
set -e
cd "$(dirname "$0")"
mkdir -p testdata
if command -v ffmpeg >/dev/null 2>&1; then
    [ -f testdata/tone.wav ]  || ffmpeg -v error -f lavfi -i "sine=frequency=440:duration=2"  -ac 2 testdata/tone.wav
    [ -f testdata/tone2.wav ] || ffmpeg -v error -f lavfi -i "sine=frequency=660:duration=2"  -ac 2 testdata/tone2.wav
elif command -v sox >/dev/null 2>&1; then
    [ -f testdata/tone.wav ]  || sox -n -r 44100 -c 2 testdata/tone.wav  synth 2 sine 440
    [ -f testdata/tone2.wav ] || sox -n -r 44100 -c 2 testdata/tone2.wav synth 2 sine 660
else
    echo "need ffmpeg or sox to generate test tones" >&2
    exit 1
fi
echo "testdata ready:"
ls -l testdata
