# AAC edits and timecode regression

`aac-edits-timecode.mov` is a generated 64×32 BT.2020 HLG ProRes HQ movie,
not user footage. It has 72 video frames, 111 stereo AAC packets at 48 kHz,
an audio edit start of 2048/48000 seconds and a stored timecode reference.
Only the unit-test bundle includes this fixture.

SHA-256: `8e5cfdde833462833f5acae2a46e0b710a4ff0541f676c628732abaf173266ad`.

Generated with the existing development FFmpeg 8.1.2 executable:

```sh
ffmpeg -v error -y \
  -f lavfi -i 'color=c=gray:s=64x32:r=30:d=2.4' \
  -itsoffset 0.064 -f lavfi -i 'sine=frequency=997:sample_rate=48000:duration=2.346666666667' \
  -map 0:v -map 1:a \
  -vf 'setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc:range=limited' \
  -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le -threads:v 2 \
  -color_primaries bt2020 -color_trc arib-std-b67 -colorspace bt2020nc -color_range tv \
  -c:a aac -ac 2 -b:a 128k -timecode '13:48:00:00' \
  -movie_timescale 48000 -movflags '+write_colr' aac-edits-timecode.mov
```

The app does not invoke this executable. Regression tests run the native Swift
pipeline, repeat strict packet/timing checks, and exercise original MOV table
restoration, media bounds and 64-bit duration promotion. The reported camera
MOV has not been supplied; this fixture reproduces its AAC count/start values,
not its original encoded payload or complete track structure. On the tested
iOS simulator, AVAssetReader normalizes this fixture's audio start to zero and
does not expose its timecode association through loadAssociatedTracks. Tests
compare the source/output as observed by AVFoundation and also check the raw
MOV association. A separate damaged-payload regression exercises production
validation, restoration and validation again, without assuming reader behavior
matches ffprobe or the camera report.
