/* miniaudio implementation translation unit.
   Compiled separately and cached so the huge single-header library
   is only built once and does not slow down rebuilds of the game.

   We trim miniaudio aggressively: only the raw playback device path
   plus the platform's native backend are needed. Disabling the
   decoders/encoders/engine/resource-manager slashes compile time. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_WAV

#ifdef _WIN32
  #define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
  #define MA_ENABLE_WASAPI
  #define MA_ENABLE_WINMM
#else
  #define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
  #define MA_ENABLE_ALSA
  #define MA_ENABLE_PULSEAUDIO
#endif

#define MA_IMPLEMENTATION
#include "miniaudio.h"
