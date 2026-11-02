ggmorse - vendored for wfview IC-7610 EXT v49 CW decoder
=========================================================

Upstream : https://github.com/ggerganov/ggmorse
Commit   : 7b4822a (2026-08-24)
License  : MIT (see LICENSE in this folder). Compatible with wfview's GPLv3.

Files copied unchanged from upstream:
  include/ggmorse/ggmorse.h
  src/resampler.cpp src/resampler.h src/goertzel.h src/stfft.h
  src/filter.h src/fft.h

Local change (src/ggmorse.cpp only):
  Upstream printf()s every decoded character to stdout. Those four calls
  are wrapped in GGMORSE_ECHO(...), which compiles to nothing unless
  GGMORSE_STDOUT_ECHO is defined. No decoding logic was changed.

Used by: include/ic7610cwcore.h (Qt-free wrapper) and
         src/ic7610cwdecoder.cpp (worker thread + window).
