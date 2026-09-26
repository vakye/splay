# splay
CLI tool for playing a .wav audio file using Linux's ALSA API

## Building
You will need the `clang` compiler before building.

```
> ./build.sh
```

## Usage
Running `splay -h` will print out usage information.

```
> splay -h
Usage: splay [Flags] <.wav Audio File>
Flags:
    -l, --loop: Enable loop playback.
    -h, --help: Print usage.
```
