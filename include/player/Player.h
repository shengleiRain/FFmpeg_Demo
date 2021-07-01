#ifndef _PLAYER_H
#define _PLAYER_H

extern "C"
{
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "Decoder.h"
#include <SDL2/SDL.h>
#include <libavutil/time.h>
}

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SAMPLE_QUEUE_SIZE 9

#define FF_QUIT_EVENT (SDL_USEREVENT + 2)
#define FF_REFRESH_TIMER (SDL_USEREVENT + 1)

int read_thread(void *arg);
int video_thread(void *arg);

Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque);

class Player;

class VideoState
{
public:
    //file
    char *filename;
    AVFormatContext *format_ctx = NULL;
    AVInputFormat *iformat = NULL;
    SDL_Thread *read_tid; //读取线程id
    int eof = 0;          //是否到文件末尾

    //video related
    int video_last_stream_index;
    int video_stream_index;
    AVStream *video_stream = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    PacketQueue *video_queue = NULL;
    FrameQueue *video_frame_queue = NULL;
    struct SwsContext *video_sws_ctx = NULL;
    SDL_Texture *video_texture;
    Decoder *video_decoder = NULL;

    //时间相关
    double video_current_pts;
    int64_t video_current_pts_time;
    double frame_timer;
    double frame_last_pts;
    double frame_last_delay;

    //audio related
    int audio_last_stream_index;
    int audio_stream_index;
    AVStream *audio_stream = NULL;
    PacketQueue *audio_queue = NULL;
    FrameQueue *audio_frame_queue = NULL;
    Decoder *audio_decoder = NULL;
    struct SwrContext *audio_swr_ctx = NULL;
    //quit
    int abort_request = 0;

    //player
    Player *player = NULL;

private:
public:
    VideoState()
    {
    }

    int stream_componet_open(int stream_index)
    {
        int ret = 0;
        AVCodecContext *codec_ctx;
        const AVCodec *codec;
        if (stream_index < 0 || stream_index > format_ctx->nb_streams)
        {
            return -1;
        }

        codec_ctx = avcodec_alloc_context3(NULL);
        if (!codec_ctx)
        {
            return AVERROR(ENOMEM);
        }

        ret = avcodec_parameters_to_context(codec_ctx, format_ctx->streams[stream_index]->codecpar);
        if (ret < 0)
        {
            goto fail;
        }
        codec_ctx->pkt_timebase = format_ctx->streams[stream_index]->time_base;

        codec = avcodec_find_decoder(codec_ctx->codec_id);
        if (!codec)
        {
            ret = -1;
            goto fail;
        }
        //打开编解码器
        ret = avcodec_open2(codec_ctx, codec, NULL);
        if (ret < 0)
        {
            loge("Failed to open codec.\n");
            goto fail;
        }

        //可以开始解码了
        switch (codec_ctx->codec_type)
        {
        case AVMEDIA_TYPE_AUDIO:
            audio_stream_index = stream_index;
            audio_stream = format_ctx->streams[stream_index];
            break;
        case AVMEDIA_TYPE_VIDEO:
            video_stream_index = stream_index;
            video_stream = format_ctx->streams[stream_index];

            frame_timer = (double)av_gettime() / 1000000.0;
            frame_last_delay = 40e-3;
            video_current_pts_time = av_gettime();

            video_decoder = new Decoder();
            ret = video_decoder->init(codec_ctx, video_queue, video_frame_queue, NULL);
            if (ret < 0)
            {
                loge("Failed to init video decoder.\n");
                goto fail;
            }
            ret = video_decoder->start(video_thread, "video_thread", this);
            if (ret < 0)
            {
                goto out;
            }
            break;

        default:
            break;
        }
        goto out;

    fail:
        avcodec_free_context(&codec_ctx);
    out:
        return ret;
    }

    void stream_component_close(int stream_index)
    {
        AVCodecParameters *codecpar;

        if (stream_index < 0 || stream_index >= format_ctx->nb_streams)
            return;
        codecpar = format_ctx->streams[stream_index]->codecpar;

        switch (codecpar->codec_type)
        {
        case AVMEDIA_TYPE_AUDIO:
            if (audio_decoder != NULL)
            {
                delete audio_decoder;
                audio_decoder = NULL;
            }
            audio_stream_index = -1;
            audio_stream = NULL;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (video_decoder != NULL)
            {
                delete video_decoder;
                video_decoder = NULL;
            }
            video_stream_index = -1;
            video_stream = NULL;
            break;

        default:
            break;
        }
    }

    int init(const char *filename, const AVInputFormat *iformat)
    {
        int ret = 0;
        int err;
        int video_index = -1, audio_index = -1;
        this->filename = av_strdup(filename);
        if (!filename)
        {
            ret = -1;
            goto fail;
        }
        this->iformat = const_cast<AVInputFormat *>(iformat);
        this->video_last_stream_index = this->video_stream_index = -1;
        this->audio_last_stream_index = this->audio_stream_index = -1;

        this->video_queue = new PacketQueue();
        if (!this->video_queue || this->video_queue->init() < 0)
        {
            ret = -1;
            goto fail;
        }

        this->audio_queue = new PacketQueue();
        if (!this->audio_queue || this->audio_queue->init() < 0)
        {
            ret = -1;
            goto fail;
        }

        this->video_frame_queue = new FrameQueue();
        if (!this->video_frame_queue || this->video_frame_queue->init(VIDEO_PICTURE_QUEUE_SIZE, video_queue) < 0)
        {
            ret = -1;
            goto fail;
        }

        this->audio_frame_queue = new FrameQueue();
        if (!this->audio_frame_queue || this->audio_frame_queue->init(SAMPLE_QUEUE_SIZE, audio_queue) < 0)
        {
            ret = -1;
            goto fail;
        }

        read_tid = SDL_CreateThread(read_thread, "read_thread", this);
        if (!read_tid)
        {
            ret = -1;
            loge("SDL_CreateThread(): %s\n", SDL_GetError());
        fail:
            destory();
        }

        return ret;
    }

    void destory()
    {
        abort_request = 1;
        // SDL_WaitThread(read_tid, NULL);
        if (video_stream_index >= 0)
        {
            stream_component_close(video_stream_index);
        }
        if (audio_stream_index >= 0)
        {
            stream_component_close(audio_stream_index);
        }
        if (format_ctx != NULL)
        {
            avformat_close_input(&format_ctx);
        }

        if (video_queue)
        {
            delete video_queue;
            video_queue = NULL;
        }
        if (video_frame_queue)
        {
            delete video_frame_queue;
            video_frame_queue = NULL;
        }
        if (audio_queue)
        {
            delete audio_queue;
            audio_queue = NULL;
        }
        if (audio_frame_queue)
        {
            delete audio_frame_queue;
            audio_frame_queue = NULL;
        }

        if (video_sws_ctx != NULL)
        {
            sws_freeContext(video_sws_ctx);
        }
        if (audio_swr_ctx != NULL)
        {
            swr_free(&audio_swr_ctx);
        }

        av_free(filename);
    }
    ~VideoState()
    {
        logi("~VideoState()\n");
        destory();
    }
};

class Player
{
public:
    VideoState *state;
    int width = 0, height = 0;
    int top = 0, left = 0;
    int screen_width = 0, screen_height = 0;
    int default_width = 640, default_height = 480;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;

    int quit=0;

private:
    void calculate_display_rect(SDL_Rect *rect, int left, int top, int max_width, int max_height, int width, int height, AVRational sar);
    void video_refresh_timer(void *arg);
    void schedule_refresh(int delay);
    void video_display(Frame *frame);
    void video_open();
    int upload_texture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx);
    int realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);

public:
    Player(/* args */);
    int open(const char *filename, const AVInputFormat *iformat);
    void set_default_window_size(int width, int height, AVRational sar);
    void close();
    ~Player();
};

#endif