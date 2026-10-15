/*
Copyright (C) 2022 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "sound.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

typedef struct {
    qhandle_t file;
    int fmt;
    char *path;
} music_handle_t;

typedef struct {
    AVFormatContext     *fmt_ctx;
    AVIOContext         *avio_ctx;
    AVCodecContext      *dec_ctx;
    qhandle_t           file;
    int                 stream_index;
    char                autotrack[MAX_QPATH];
    byte                avio_ctx_buffer[4096];
} music_state_t;

static music_state_t        music;
static char                 music_game_track[MAX_QPATH];

static AVPacket             *music_pkt;
static AVFrame              *music_frame_in;
static AVFrame              *music_frame_out;
static struct SwrContext    *music_swr_ctx;
static bool                 music_swr_draining;
static bool                 music_manual_play;
static bool                 music_paused;

static cvar_t   *music_enable;
static cvar_t   *music_volume;
static cvar_t   *music_shuffle;
static cvar_t   *music_menu_track;

static void     **tracklist;
static int      trackcount;
static int      trackindex;

static char     extensions[MAX_QPATH];
static int      supported;

static const avformat_t formats[] = {
    { ".flac", "flac", AV_CODEC_ID_FLAC },
    { ".opus", "ogg", AV_CODEC_ID_OPUS },
    { ".ogg", "ogg", AV_CODEC_ID_VORBIS },
    { ".mp3", "mp3", AV_CODEC_ID_MP3 },
    { ".wav", "wav", AV_CODEC_ID_NONE }
};

static void init_formats(void)
{
    for (int i = 0; i < q_countof(formats); i++) {
        const avformat_t *f = &formats[i];
        if (!av_find_input_format(f->fmt))
            continue;
        if (f->codec_id != AV_CODEC_ID_NONE &&
            !avcodec_find_decoder(f->codec_id))
            continue;
        supported |= BIT(i);
        if (*extensions)
            Q_strlcat(extensions, ";", sizeof(extensions));
        Q_strlcat(extensions, f->ext, sizeof(extensions));
    }

    Com_DPrintf("Supported music formats: %s\n", extensions);
}

static const avformat_t *find_format(const char *ext)
{
    for (int i = 0; i < q_countof(formats); i++)
        if (!Q_stricmp(ext, formats[i].ext))
            return &formats[i];
    return NULL;
}

static void music_close(void)
{
    avcodec_free_context(&music.dec_ctx);
    avformat_close_input(&music.fmt_ctx);
    avio_context_free(&music.avio_ctx);
    FS_CloseFile(music.file);

    memset(&music, 0, sizeof(music));
}

static int vfs_read_packet(void *opaque, uint8_t *buf, int size)
{
    int ret = FS_Read(buf, size, music.file);
    if (ret == 0)
        return AVERROR_EOF;
    if (ret < 0)
        return AVERROR(EINVAL);
    return ret;
}

static int64_t vfs_seek(void *opaque, int64_t offset, int whence)
{
    if (whence == AVSEEK_SIZE)
        return FS_Length(music.file);

    int ret = FS_Seek(music.file, offset, whence);
    if (ret < 0)
        return AVERROR(EINVAL);

    return FS_Tell(music.file);
}

static bool music_play(music_handle_t h)
{
    const AVInputFormat *fmt;
    const AVStream      *st;
    const AVCodec       *dec;
    int                 ret;

    Q_assert(!music.file);
    music.file = h.file;

    fmt = av_find_input_format(formats[h.fmt].fmt);
    if (!fmt) {
        Com_EPrintf("Failed to find input format %s\n", formats[h.fmt].fmt);
        goto fail;
    }

    music.fmt_ctx = avformat_alloc_context();
    if (!music.fmt_ctx) {
        Com_EPrintf("Failed to allocate format context\n");
        goto fail;
    }

    music.avio_ctx = avio_alloc_context(music.avio_ctx_buffer, sizeof(music.avio_ctx_buffer),
                                        0, NULL, vfs_read_packet, NULL, vfs_seek);
    if (!music.avio_ctx) {
        Com_EPrintf("Failed to allocate avio context\n");
        goto fail;
    }

    music.fmt_ctx->pb = music.avio_ctx;

    ret = avformat_open_input(&music.fmt_ctx, h.path, fmt, NULL);
    if (ret < 0) {
        Com_EPrintf("Couldn't open %s: %s\n", h.path, av_err2str(ret));
        goto fail;
    }

    ret = avformat_find_stream_info(music.fmt_ctx, NULL);
    if (ret < 0) {
        Com_EPrintf("Couldn't find stream info: %s\n", av_err2str(ret));
        goto fail;
    }

#if USE_DEBUG
    if (developer->integer)
        AV_DumpFormat(music.fmt_ctx);
#endif

    ret = av_find_best_stream(music.fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret < 0) {
        Com_EPrintf("Couldn't find audio stream\n");
        goto fail;
    }

    music.stream_index = ret;
    st = music.fmt_ctx->streams[music.stream_index];

    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        Com_EPrintf("Failed to find audio codec %s\n", avcodec_get_name(st->codecpar->codec_id));
        goto fail;
    }

    music.dec_ctx = avcodec_alloc_context3(dec);
    if (!music.dec_ctx) {
        Com_EPrintf("Failed to allocate audio codec context\n");
        goto fail;
    }

    ret = avcodec_parameters_to_context(music.dec_ctx, st->codecpar);
    if (ret < 0) {
        Com_EPrintf("Failed to copy audio codec parameters to decoder context\n");
        goto fail;
    }

    ret = avcodec_open2(music.dec_ctx, dec, NULL);
    if (ret < 0) {
        Com_EPrintf("Failed to open audio codec\n");
        goto fail;
    }

    music.dec_ctx->pkt_timebase = st->time_base;

    Com_DPrintf("Playing %s\n", h.path);
    Z_Free(h.path);
    return true;

fail:
    Z_Free(h.path);
    music_close();
    return false;
}

static void shuffle(void)
{
    for (int i = trackcount - 1; i > 0; i--) {
        int j = Q_rand_uniform(i + 1);
        SWAP(void *, tracklist[i], tracklist[j]);
    }
}

static music_handle_t open_track(const char *name)
{
    char path[MAX_OSPATH];
    qhandle_t f;

    for (int i = 0; i < q_countof(formats); i++) {
        if (!(supported & BIT(i)))
            continue;
        if (Q_snprintf(path, sizeof(path), "music/%s%s", name, formats[i].ext) >= sizeof(path))
            break;
        FS_OpenFile(path, &f, FS_MODE_READ);
        if (f)
            return (music_handle_t){ .file = f, .fmt = i, .path = Z_CopyString(path) };
    }

    return (music_handle_t){ 0 };
}

static music_handle_t open_track_ext(const char *name)
{
    if (COM_IsUint(name)) {
        int track = Q_atoi(name);
        if (track <= 0)
            return (music_handle_t){ 0 };
        return open_track(va("track%02d", track));
    } else {
        // strip extension and lookup first possible format
        char *ext = COM_FileExtension(name);
        if (find_format(ext))
            name = va("%.*s", (int)(ext - name), name);
        return open_track(name);
    }
}

void Music_Play(void)
{
    const char *s;
    music_handle_t h;

    if (!s_started || cls.state == ca_cinematic || music_manual_play)
        return;

    if (cls.state >= ca_connected)
        s = music_game_track;
    else
        s = music_menu_track->string;

    if (!*s || !strcmp(s, "0")) {
        Music_Stop();
        return;
    }

    // don't restart the same track
    if (!Q_stricmp(music.autotrack, s))
        return;

    // drop samples if server is changing tracks
    if (music.fmt_ctx)
        Music_Stop();

    // don't start new track if auto playback disabled
    if (!music_enable->integer)
        return;

    Q_strlcpy(music.autotrack, s, sizeof(music.autotrack));

    if (music_shuffle->integer && trackcount) {
        for (int i = 0; i < trackcount; i++) {
            if (trackindex == 0)
                shuffle();
            h = open_track(tracklist[trackindex]);
            trackindex = (trackindex + 1) % trackcount;
            if (h.file && music_play(h))
                break;
        }
    } else {
        h = open_track_ext(s);
        if (h.file)
            music_play(h);
        else
            Com_DPrintf("No such track: %s\n", s);
    }
}

void Music_Stop(void)
{
    Com_DPrintf("Stopping music playback\n");
    music_close();

    av_frame_unref(music_frame_in);
    av_frame_unref(music_frame_out);
    if (music_swr_ctx)
        swr_close(music_swr_ctx);

    music_swr_draining = false;
    music_manual_play = false;
    music_paused = false;

    if (s_api)
        s_api->drop_raw_samples();
}

void S_StartBackgroundTrack(const char *track)
{
    Q_strlcpy(music_game_track, track, sizeof(music_game_track));
    Music_Play();
}

void S_StopBackgroundTrack(void)
{
    music_game_track[0] = 0;
    Music_Stop();
}

static int read_packet(AVPacket *pkt)
{
    while (1) {
        int ret = av_read_frame(music.fmt_ctx, pkt);
        if (ret < 0)
            return ret;
        if (pkt->stream_index == music.stream_index)
            return ret;
        av_packet_unref(pkt);
    }
}

static int decode_frame(void)
{
    AVCodecContext *dec = music.dec_ctx;
    AVPacket *pkt = music_pkt;

    while (1) {
        int ret = avcodec_receive_frame(dec, music_frame_in);
        if (ret != AVERROR(EAGAIN))
            return ret;

        ret = read_packet(pkt);
        if (ret == AVERROR_EOF) {
            ret = avcodec_send_packet(dec, NULL);
            if (ret < 0)
                return ret;
        } else if (ret >= 0) {
            ret = avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            if (ret == AVERROR(EAGAIN))
                return AVERROR_BUG;
        } else {
            return ret;
        }
    }
}

static bool music_rewind(void)
{
    if (music_manual_play || !music_enable->integer || music_shuffle->integer)
        return false;

    int ret = av_seek_frame(music.fmt_ctx, music.stream_index, 0, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        return false;

    avcodec_flush_buffers(music.dec_ctx);
    ret = decode_frame();
    if (ret < 0)
        return false;

    Com_DPrintf("Rewind successful\n");
    return true;
}

static bool decode_next_frame(void)
{
    if (!music.dec_ctx)
        return false;

    int ret = decode_frame();
    if (ret >= 0)
        return true;

    if (ret == AVERROR_EOF)
        Com_DPrintf("%s decoding audio\n", av_err2str(ret));
    else
        Com_EPrintf("Error decoding audio: %s\n", av_err2str(ret));

    // try to rewind if possible
    if (music_rewind())
        return true;

    // play next file
    music_close();
    Music_Play();

    return music.dec_ctx && decode_frame() >= 0;
}

static int reconfigure_swr(void)
{
    AVFrame *in = music_frame_in;
    AVFrame *out = music_frame_out;
    int sample_rate = S_GetSampleRate();
    int ret;

    if (!sample_rate)
        sample_rate = out->sample_rate;
    if (!sample_rate)
        sample_rate = in->sample_rate;

    swr_close(music_swr_ctx);
    av_frame_unref(out);

    out->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    out->format = s_supports_float ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_S16;
    out->sample_rate = sample_rate;
    out->nb_samples = MAX_RAW_SAMPLES;

    char buf[MAX_QPATH];
    av_channel_layout_describe(&in->ch_layout, buf, sizeof(buf));

    Com_DDPrintf("Initializing SWR\n"
                 "Input : %d Hz, %s, %s\n"
                 "Output: %d Hz, stereo, %s\n",
                 in->sample_rate, buf,
                 av_get_sample_fmt_name(in->format),
                 out->sample_rate,
                 av_get_sample_fmt_name(out->format));

    ret = swr_config_frame(music_swr_ctx, out, in);
    if (ret < 0)
        return ret;

    ret = swr_init(music_swr_ctx);
    if (ret < 0)
        return ret;

    ret = av_frame_get_buffer(out, 0);
    if (ret < 0)
        return ret;

    return 0;
}

static void flush_samples(const AVFrame *out)
{
    Com_DDDPrintf("%d raw samples\n", out->nb_samples);

    raw_samples_t raw = {
        .nb_samples = out->nb_samples,
        .sample_rate = out->sample_rate,
        .bytes_per_sample = av_get_bytes_per_sample(out->format),
        .nb_channels = out->ch_layout.nb_channels,
        .data = out->data[0],
        .volume = music_volume->value
    };

    if (!s_api->raw_samples(&raw))
        s_api->drop_raw_samples();
}

static int convert_frame(AVFrame *out, AVFrame *in)
{
    int ret = swr_convert_frame(music_swr_ctx, out, in);
    if (ret < 0)
        return ret;
    in->nb_samples = 0; // don't convert again
    return 0;
}

static int convert_audio(void)
{
    AVFrame *in = music_frame_in;
    AVFrame *out = music_frame_out;
    int ret = 0, have = 0;

    if (music_paused)
        return 0;

    // get available free space
    if (!s_api->need_raw_samples())
        return 0;

    out->nb_samples = MAX_RAW_SAMPLES;

drain:
    if (music_swr_draining) {
        ret = swr_convert_frame(music_swr_ctx, out, NULL);
        if (ret < 0)
            return ret;
        if (out->nb_samples) {
            flush_samples(out);
            return 0;
        }

        Com_DDPrintf("Draining done\n");
        music_swr_draining = false;

        if (!music.dec_ctx) {
            // playback just stopped
            swr_close(music_swr_ctx);
            av_frame_unref(in);
            av_frame_unref(out);
            return 0;
        }

        ret = reconfigure_swr();
        if (ret < 0)
            return ret;
        ret = convert_frame(NULL, in);
        if (ret < 0)
            return ret;
    }

    // see how much we buffered
    if (swr_is_initialized(music_swr_ctx)) {
        have = swr_get_out_samples(music_swr_ctx, 0);
        if (have < 0)
            return have;
    }

    // buffer more frames to fill available space
    while (have < MAX_RAW_SAMPLES) {
        if (!decode_next_frame()) {
            if (!swr_is_initialized(music_swr_ctx))
                return 0;
            Com_DDPrintf("No next frame, draining\n");
            music_swr_draining = true;
            goto drain;
        }

        // work around swr channel layout bug
        if (in->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&in->ch_layout, in->ch_layout.nb_channels);

        // now that we have a frame, configure swr
        if (!swr_is_initialized(music_swr_ctx)) {
            ret = reconfigure_swr();
            if (ret < 0)
                return ret;
        }

        have = swr_get_out_samples(music_swr_ctx, in->nb_samples);
        if (have < 0)
            return have;
        if (have < MAX_RAW_SAMPLES) {
            ret = convert_frame(NULL, in);
            if (ret < 0)
                break;
        }
    }

    // now output what we have
    if (ret >= 0)
        ret = convert_frame(out, in);

    if (ret == AVERROR_INPUT_CHANGED) {
        // wait for swr buffer to drain, then reconfigure
        Com_DDPrintf("Input changed, draining\n");
        music_swr_draining = true;
        goto drain;
    }

    if (ret < 0)
        return ret;

    if (out->nb_samples)
        flush_samples(out);

    return 0;
}

void Music_Update(void)
{
    if (!s_started || !s_active)
        return;

    if (!music.dec_ctx && !music_swr_draining) {
        // resume auto playback if manual playback just stopped
        if (music_manual_play && !s_api->have_raw_samples()) {
            music_manual_play = false;
            Music_Play();
        }
        if (!music.dec_ctx)
            return;
    }

    int ret = convert_audio();
    if (ret < 0) {
        Com_EPrintf("Error converting audio: %s\n", av_err2str(ret));
        Music_Stop();
    }
}

static void free_track_list(void)
{
    FS_FreeList(tracklist);
    tracklist  = NULL;
    trackcount = trackindex = 0;
}

void Music_LoadTrackList(void)
{
    if (!*extensions)
        return;

    free_track_list();
    tracklist = FS_ListFiles("music", extensions, FS_SEARCH_RECURSIVE | FS_SEARCH_STRIPEXT, &trackcount);
    Com_DPrintf("Found %d music tracks.\n", trackcount);
}

static void Music_Play_f(void)
{
    if (Cmd_Argc() < 3) {
        Com_Printf("Usage: %s %s <track>\n", Cmd_Argv(0), Cmd_Argv(1));
        return;
    }

    if (!s_started) {
        Com_Printf("Sound system not started.\n");
        return;
    }

    if (cls.state == ca_cinematic) {
        Com_Printf("Can't play music in cinematic mode.\n");
        return;
    }

    music_handle_t h = open_track_ext(Cmd_Argv(2));
    if (!h.file) {
        Com_Printf("No such track: %s\n", Cmd_Argv(2));
        return;
    }

    if (!strcmp(Cmd_Argv(3), "soft"))
        music_close();
    else
        Music_Stop();

    if (music_play(h))
        music_manual_play = true;
}

static void Music_Info_f(void)
{
    if (music.fmt_ctx) {
        AV_DumpFormat(music.fmt_ctx);
    } else {
        Com_Printf("Playback stopped.\n");
    }
}

static void Music_Cmd_c(int firstarg, int argnum)
{
    if (argnum == 1) {
        Prompt_AddMatch("info");
        Prompt_AddMatch("play");
        Prompt_AddMatch("stop");
        Prompt_AddMatch("next");
        Prompt_AddMatch("pause");
        return;
    }

    if (argnum == 2 && !strcmp(Cmd_Argv(firstarg + 1), "play")) {
        Prompt_SetOptions(CMPL_CASELESS);
        for (int i = 0; i < trackcount; i++)
            Prompt_AddMatch(tracklist[i]);
    }
}

static void Music_Next_f(void)
{
    if (!s_started) {
        Com_Printf("Sound system not started.\n");
        return;
    }

    if (cls.state == ca_cinematic) {
        Com_Printf("Can't play music in cinematic mode.\n");
        return;
    }

    music_manual_play = false;
    music.autotrack[0] = 0;

    Music_Play();
}

static void Music_Pause_f(void)
{
    if (!music.dec_ctx) {
        Com_Printf("Playback stopped.\n");
        return;
    }

    if (Cmd_Argc() > 2)
        music_paused = Q_atoi(Cmd_Argv(2));
    else
        music_paused ^= true;

    S_PauseRawSamples(music_paused);
}

static void Music_Cmd_f(void)
{
    const char *cmd = Cmd_Argv(1);

    if (!strcmp(cmd, "info"))
        Music_Info_f();
    else if (!strcmp(cmd, "play"))
        Music_Play_f();
    else if (!strcmp(cmd, "stop"))
        Music_Stop();
    else if (!strcmp(cmd, "next"))
        Music_Next_f();
    else if (!strcmp(cmd, "pause") || !strcmp(cmd, "toggle"))
        Music_Pause_f();
    else
        Com_Printf("Usage: %s <info|play|stop|next|pause>\n", Cmd_Argv(0));
}

static void music_enable_changed(cvar_t *self)
{
    if (cls.state == ca_cinematic || music_manual_play)
        return;

    // pause/resume if already playing
    if (music.dec_ctx) {
        music_paused = !self->integer;
        S_PauseRawSamples(music_paused);
        return;
    }

    if (self->integer)
        Music_Play();
    else
        Music_Stop();
}

static void music_volume_changed(cvar_t *self)
{
    Cvar_ClampValue(self, 0, 1);
}

static void music_menu_track_changed(cvar_t *self)
{
    if (cls.state < ca_connected)
        Music_Play();
}

static const cmdreg_t c_music[] = {
    { "music", Music_Cmd_f, Music_Cmd_c },
    { NULL }
};

void Music_Init(void)
{
    music_enable = Cvar_Get("music_enable", "1", 0);
    music_enable->changed = music_enable_changed;
    music_volume = Cvar_Get("music_volume", "1", 0);
    music_volume->changed = music_volume_changed;
    music_shuffle = Cvar_Get("music_shuffle", "0", 0);
    music_menu_track = Cvar_Get("music_menu_track", "0", 0);
    music_menu_track->changed = music_menu_track_changed;

    Cmd_Register(c_music);

    init_formats();

    Music_LoadTrackList();

    Q_assert(music_pkt = av_packet_alloc());
    Q_assert(music_frame_in = av_frame_alloc());
    Q_assert(music_frame_out = av_frame_alloc());
    Q_assert(music_swr_ctx = swr_alloc());
}

void Music_Shutdown(void)
{
    music_close();

    av_packet_free(&music_pkt);
    av_frame_free(&music_frame_in);
    av_frame_free(&music_frame_out);
    swr_free(&music_swr_ctx);

    music_swr_draining = false;
    music_manual_play = false;
    music_paused = false;

    free_track_list();
}
