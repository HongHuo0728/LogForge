# AAC edits and timecode regression

`aac-edits-timecode.mov` is a generated 64×32 BT.2020 HLG ProRes HQ movie,
not user footage. It has 72 video frames, 111 stereo AAC packets at 48 kHz,
an audio start of 2048/48000 seconds and an associated timecode track.
Only the unit-test bundle includes this fixture.

SHA-256: `c7b45bab98e9d0e3aee030b7100d4a39537ea2c8f7c9853e83bc698a2afeca51`.

Generated with the existing development FFmpeg 8.1.2 executable:

```sh
ffmpeg -v error -y \
  -f lavfi -i 'color=c=gray:s=64x32:r=30:d=2.4' \
  -itsoffset 0.064 -f lavfi -i 'sine=frequency=997:sample_rate=48000:duration=2.346666666667' \
  -map 0:v -map 1:a -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le -threads:v 2 \
  -color_primaries bt2020 -color_trc arib-std-b67 -colorspace bt2020nc -color_range tv \
  -c:a aac -ac 2 -b:a 128k -timecode '13:48:00:00' \
  -movie_timescale 48000 -movflags '+write_colr' aac-edits-timecode.mov
```

The app does not invoke this executable. Regression tests run the native Swift
pipeline, repeat strict packet/timing checks, and exercise original MOV table
restoration, media bounds and 64-bit duration promotion. The reported camera
MOV has not been supplied; this fixture reproduces its AAC count/start values,
not its original encoded payload or complete track structure.
