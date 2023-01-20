#ifndef _BAMBU__TUNNEL_H_
#define _BAMBU__TUNNEL_H_

#ifdef BAMBU_DYNAMIC
#  define BAMBU_EXPORT
#  define BAMBU_FUNC(x) (*x)
#else
#  ifdef _WIN32
#    ifdef BAMBU_EXPORTS
#      define BAMBU_EXPORT __declspec(dllexport)
#    else
#      define BAMBU_EXPORT __declspec(dllimport)
#    endif // BAMBU_EXPORTS
#  else
#    define BAMBU_EXPORT
#  endif // __WIN32__
#  define BAMBU_FUNC(x) x
#endif // BAMBU_DYNAMIC

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifndef __cplusplus
#include <stdbool.h>

/* We need these workarounds since we're compiling C source, not C++. */
typedef enum Bambu_StreamType Bambu_StreamType;
typedef struct Bambu_StreamInfo Bambu_StreamInfo;
typedef struct Bambu_Sample Bambu_Sample;
#endif

#ifdef _WIN32
    typedef wchar_t tchar;
#else
    typedef char tchar;
#endif

typedef void* Bambu_Tunnel;

typedef void (*Logger)(void * context, int level, tchar const* msg);

enum Bambu_StreamType
{
    VIDE,
    AUDI
};

enum Bambu_VideoSubType
{
    AVC1,
};

enum Bambu_AudioSubType
{
    MP4A
};

enum Bambu_FormatType
{
    video_avc_packet,
    video_avc_byte_stream,
    audio_raw,
    audio_adts
};

struct Bambu_StreamInfo
{
    Bambu_StreamType type;
    int sub_type;
    union {
        struct
        {
            int width;
            int height;
            int frame_rate;
        } video;
        struct
        {
            int sample_rate;
            int channel_count;
            int sample_size;
        } audio;
    } format;
    int format_type;
    int format_size;
    unsigned char const * format_buffer;
};

enum Bambu_SampleFlag
{
    f_sync = 1
};

struct Bambu_Sample
{
    int itrack;
    int size;
    int flags;
    unsigned char const * buffer;
    unsigned long long decode_time;
};

enum Bambu_Error
{
    Bambu_success,
    Bambu_stream_end,
    Bambu_would_block, 
    Bambu_buffer_limit
};

#ifdef BAMBU_DYNAMIC
struct BambuLib {
#endif

BAMBU_EXPORT int BAMBU_FUNC(Bambu_Create)(Bambu_Tunnel* tunnel, char const* path);

BAMBU_EXPORT void BAMBU_FUNC(Bambu_SetLogger)(Bambu_Tunnel tunnel, Logger logger, void * context);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_Open)(Bambu_Tunnel tunnel);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_StartStream)(Bambu_Tunnel tunnel, bool video);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_GetStreamCount)(Bambu_Tunnel tunnel);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_GetStreamInfo)(Bambu_Tunnel tunnel, int index, Bambu_StreamInfo* info);

BAMBU_EXPORT unsigned long BAMBU_FUNC(Bambu_GetDuration)(Bambu_Tunnel tunnel);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_Seek)(Bambu_Tunnel tunnel, unsigned long time);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_ReadSample)(Bambu_Tunnel tunnel, Bambu_Sample* sample);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_SendMessage)(Bambu_Tunnel tunnel, int ctrl, char const* data, int len);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_RecvMessage)(Bambu_Tunnel tunnel, int* ctrl, char* data, int* len);

BAMBU_EXPORT void BAMBU_FUNC(Bambu_Close)(Bambu_Tunnel tunnel);

BAMBU_EXPORT void BAMBU_FUNC(Bambu_Destroy)(Bambu_Tunnel tunnel);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_Init)();

BAMBU_EXPORT void BAMBU_FUNC(Bambu_Deinit)();

BAMBU_EXPORT char const* BAMBU_FUNC(Bambu_GetLastErrorMsg)();

BAMBU_EXPORT void BAMBU_FUNC(Bambu_FreeLogMsg)(tchar const* msg);

#ifdef BAMBU_DYNAMIC
};
#endif

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _BAMBU__TUNNEL_H_
