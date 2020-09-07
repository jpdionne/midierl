
build:
	clang coremidi.cpp -framework CoreMIDI -framework CoreFoundation -o coremidi
	erl  -noshell -eval "c:c(midi),init:stop()"

run: 
	erl  -noshell -eval "c:l(midi),midi:start()"
