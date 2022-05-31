# RTCast - WebRTC real-time audio and video casting

RTCast is a WebRTC real-time audio and video casting tool based on [libdatachannel](https://github.com/paullouisageneau/libdatachannel) and [FFmpeg](https://ffmpeg.org/). It can also get video from [libcamera](https://libcamera.org/) on supported platforms.

Licensed under MPL 2.0, see [LICENSE](https://github.com/paullouisageneau/rtcast/blob/master/LICENSE).

# Building

RTCast requires [FFmpeg](https://ffmpeg.org/) and optionally [libcamera](https://libcamera.org/).

Clone the repository and initialize submodules:
```
$ git clone https://github.com/paullouisageneau/rtcast.git
$ cd rtcast
$ git submodule update --init --recursive --depth 1
```

Build with CMake:
```
$ cmake -B build
$ (cd build; make -j4)
```

# Running

```
$ python -m http.server &
$ build/rtcast
```

Then point a browser to http://localhost:8000/ to get the audio and video stream.

