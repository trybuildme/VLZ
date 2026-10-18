# vlz — minimal VLC-3-style player built on libvlc.so.5
# No libvlc dev headers on this system: all declarations are hand-written in vlz.c
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
LDLIBS   = -l:libvlc.so.5 -lpthread -ldl

vlz: vlz.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# Headless smoke tests (need ffmpeg or sox for tone generation)
test: vlz
	./mktestdata.sh
	./vlz --version
	./vlz --intf=dummy --aout=dummy --no-control testdata/tone.wav </dev/null; echo "exit=$$?"
	printf 'status\nq\n' | ./vlz --intf=dummy --aout=dummy testdata/tone.wav testdata/tone2.wav; echo "exit=$$?"
	./vlz --intf=dummy --aout=dummy --no-control testdata/missing.wav </dev/null; echo "exit=$$? (expect 1)"

clean:
	rm -f vlz

.PHONY: test clean
