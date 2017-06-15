FFmpeg README
=============

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides a mean to alter decoded Audio and Video through chain of filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* [ffserver](https://ffmpeg.org/ffserver.html) is a multimedia streaming server
  for live broadcasts.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process. Few developers
follow pull requests so they will likely be ignored.

LHE README
=============

##LHE-Codec

LHE image and video coder and decoder

LHE - Logarithmical Hopping Encoding - is a new spatial domain lossy compressor, based on weber-Feschner law
This project comprises following components:
- LHE basic image compressor/decompressor : non-elegible bit-rate
- Advanced LHE compressor/decompressor: elegible quality

###Prerequisites
You will need yasm
  ```
  sudo apt-get install yasm
  ```

It is recommendable to install OpenMP

Check gcc version:

  ```
  gcc -v 
  ```

  If gcc version is less than 4.2 do:

  ```
  sudo apt-get install gcc-4.2 
  ```

  Finally, install OpenMP:

  ```
  sudo apt-get install libgomp1
  ```
More instructions can be found here: https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu

###Compile and Install

LHE is in lhe_develop branch
  ```
  git checkout -b lhe_develop origin/lhe_develop 

  ./configure --extra-cflags=-fopenmp --extra-ldflags=-fopenmp 
  sudo make && sudo make install 
  ```


###Options

* *basic_lhe true*: necessary to play basic lhe. If this option is not set, advanced lhe will be played.
* *pix_fmt format*: format can be yuv420p, yuv422p, yuv444p. If this option is not specified, FFmpeg chooses the best suited format for input image.
* *ql* value: from 0 to 99. Quality value. If this param is not specified, ql is 50.
* *pr_metrics* true: prints PR metrics (testing purpose).
* *subsampling_average* true: Subsampling is made using average of samples. Otherwise samples are taken using single pixel selection (sps)

###Basic LHE

For example: lena.bmp image with format YUV420.

####Encode
  ```
  ffmpeg -i lena.bmp -basic_lhe true -pix_fmt yuv420p lena.lhe
  ```

####Decode
  ```
  ffmpeg -i lena.lhe lenadec.bmp
  ```

###Advanced LHE

For example: lena.bmp image with format YUV420 and QL 70

####Encode
  ```
  ffmpeg -i lena.bmp -ql 70 -pix_fmt yuv420p lena.lhe
  ```

####Decode

  ```
  ffmpeg -i lena.lhe lenadec.bmp
  ```

If extracting planes (YUV) is required:

  ```
  ffmpeg -i lena.lhe -filter_complex "extractplanes=y+u+v[y][u][v]" -map [y] lenay.bmp -map [u] lenau.bmp -map [v] lenav.bmp
  ```

###Video LHE
For example: big_buck with format YUV420.

####Encode
  ```
  ffmpeg -i big_buck.mp4 -pix_fmt yuv420p big_buck.mlhe (care with extension is mlhe instead of lhe)
  ```

####Decode
  ```
  ffmpeg -i big_buck.mlhe big_buck_dec.mp4
  ```

####Play
To use ffplay it is necessary to install sdl library and activate it
  ```
  sudo apt-get install libsdl1.2-dev
  ./configure --extra-cflags=-fopenmp --extra-ldflags=-fopenmp --enable-ffplay 
  ```

After that, you can play videos using ffplay command:

  ```
  ffplay big_buck.mlhe 
  ```

