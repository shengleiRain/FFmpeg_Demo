#include "Player.h"
#include <iostream>
#include "player_util.h"

extern "C"
{
#include <libavutil/time.h>
};

/*****************************************************/
/*                      global                       */
/*****************************************************/
int read_thread(void *arg)
{
    VideoState *state = (VideoState *)arg;
    AVPacket *pkt;
    int ret = 0;
    int err;
    int video_index = -1, audio_index = -1;
    pkt = av_packet_alloc();
    if (!pkt)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    //开始分配format context，打开输入文件等，然后将音频流和视频流等写入到队列中
    state->format_ctx = avformat_alloc_context();
    if (!state->format_ctx)
    {
        logf("Failed to alloc format context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    err = avformat_open_input(&state->format_ctx, state->filename, state->iformat, NULL);
    if (err < 0)
    {
        loge("Failed to open input file: %s\n", av_err2str(err));
        ret = -1;
        goto fail;
    }

    err = avformat_find_stream_info(state->format_ctx, NULL);
    if (err < 0)
    {
        loge("Failed to find stream info: %s\n", av_err2str(err));
        ret = -1;
        goto fail;
    }

    av_dump_format(state->format_ctx, 0, state->filename, 0);

    // 找到合适的视频流和音频流的index
    for (int i = 0; i < state->format_ctx->nb_streams; i++)
    {
        if (state->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0)
        {
            video_index = i;
        }
        if (state->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0)
        {
            audio_index = i;
        }
    }

    if (video_index >= 0)
    {
        auto *stream = state->format_ctx->streams[video_index];
        auto *codecpar = stream->codecpar;
        AVRational sar = av_guess_sample_aspect_ratio(state->format_ctx, stream, NULL);
        if (codecpar->width)
        {
            state->player->set_default_window_size(codecpar->width, codecpar->height, sar);
        }
        state->stream_componet_open(video_index);
    }

    if (audio_index >= 0)
    {
        state->stream_componet_open(audio_index);
    }

    if (video_index < 0 && audio_index < 0)
    {
        logf("Failed to find video & audio stream index.\n");
        ret = -1;
        goto fail;
    }

    //无限循环读取
    for (;;)
    {
        if (state->abort_request)
        {
            break;
        }
        ret = av_read_frame(state->format_ctx, pkt);
        if (ret < 0)
        {
            if ((ret == AVERROR_EOF || avio_feof(state->format_ctx->pb) && !state->eof))
            {
                if (state->video_stream_index >= 0)
                {
                    pkt->stream_index = state->video_stream_index;
                    state->video_queue->put(pkt);
                }
                if (state->audio_stream_index >= 0)
                {
                    pkt->stream_index = state->audio_stream_index;
                    state->audio_queue->put(pkt);
                }
                state->eof = 1;
            }
            if (state->format_ctx->pb && state->format_ctx->pb->error)
            {
                break;
            }
            continue;
        }
        else
        {
            state->eof = 0;
        }
        //在此处可以做一些其他的判断，控制packet进入到队列中。比如限制播放时长等
        if (pkt->stream_index == state->video_stream_index)
        {
            state->video_queue->put(pkt);
        }
        else if (pkt->stream_index == state->audio_stream_index)
        {
            state->audio_queue->put(pkt);
        }
        else
        {
            av_packet_unref(pkt);
        }
    }
    ret = 0;
fail:
    av_packet_free(&pkt);
    if (ret != 0)
    {
        //发送退出的事件
        SDL_Event event;
        event.type = FF_QUIT_EVENT;
        event.user.data1 = state;
        SDL_PushEvent(&event);
    }
    return ret;
}

int video_thread(void *arg)
{
    VideoState *state = (VideoState *)arg;
    AVFrame *frame = av_frame_alloc();
    int ret = 0;
    int frame_ctn = 0;
    AVRational frame_rate = av_guess_frame_rate(state->format_ctx, state->video_stream, NULL);
    if (!frame)
    {
        logf("Failed to alloc frame.\n");
        return AVERROR(ENOMEM);
    }
    for (;;)
    {
        int got_frame = state->video_decoder->decode_frame(frame);
        if (got_frame < 0)
        {
            goto end;
        }
        if (!got_frame)
        {
            continue;
        }
        if (got_frame)
        {
            int duration = (frame_rate.num && frame_rate.den) ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0;
            double pts = av_frame_get_best_effort_timestamp(frame);
            pts = pts != AV_NOPTS_VALUE ? pts : 0;
            pts *= av_q2d(state->video_stream->time_base);

            Frame *ret_frame = state->video_frame_queue->put(frame, duration, pts, frame->pkt_pos);
            //在这里设置player的宽和高

            frame_ctn++;
            logf("frame count=%d\n", frame_ctn);
            if (ret_frame)
            {
                ret = 0;
                state->player->set_default_window_size(ret_frame->width, ret_frame->height, ret_frame->sar);
            }
            else
            {
                ret = -1;
            }
            av_frame_unref(frame);
        }
        if (ret < 0)
        {
            goto end;
        }
    }
end:
    av_frame_free(&frame);
    return ret;
}

Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque)
{
    SDL_Event event;
    event.type = FF_REFRESH_TIMER;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0; /* 0 means stop timer */
}

Player::Player(/* args */)
{
}

Player::~Player()
{
}

/*****************************************************/
/*                      private                      */
/*****************************************************/
void Player::calculate_display_rect(SDL_Rect *rect, int scr_xleft, int scr_ytop, int scr_width, int scr_height, int pic_width, int pic_height, AVRational pic_sar)
{
    AVRational aspect_ratio = pic_sar;
    int64_t width, height, x, y;

    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
        aspect_ratio = av_make_q(1, 1);

    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > scr_width)
    {
        width = scr_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop + y;
    rect->w = FFMAX((int)width, 1);
    rect->h = FFMAX((int)height, 1);
}

void Player::video_refresh_timer(void *arg)
{
    double actual_delay, delay;
    Frame *vp;

    if (state->video_stream)
    {
        if (state->video_frame_queue->is_empty())
        {
            loge("video refresh do nothing...\n");
            //do nothing.
            // schedule_refresh(1); //frame queue 中没有frame，那么就延时1ms显示
        }
        else
        {
            vp = state->video_frame_queue->get();

            state->video_current_pts = vp->pts;
            state->video_current_pts_time = av_gettime();
            delay = vp->pts - state->frame_last_pts;

            if (delay <= 0 || delay >= 1.0)
            {
                delay = state->frame_last_delay;
            }

            state->frame_last_delay = delay;
            state->frame_last_pts = vp->pts;

            //TODO 音视频同步相关

            state->frame_timer += delay;
            actual_delay = state->frame_timer - (av_gettime() / 1000000.0);
            if (actual_delay < 0.010)
            {
                actual_delay = 0.010;
            }
            logi("vp pts =%f, delay=%f, actual_delay=%f\n", vp->pts, delay, actual_delay);
            schedule_refresh((int)(actual_delay * 1000 + 0.5));

            //show picture
            video_display(vp);
        }
    }
    else
    {
        schedule_refresh(100);
    }
}

void Player::schedule_refresh(int delay)
{
    SDL_AddTimer(delay, sdl_refresh_timer_cb, state);
}

void Player::video_display(Frame *vp)
{
    if (!width)
    {
        video_open();
    }

    Uint32 pixformat = SDL_PIXELFORMAT_IYUV;

    //create texture for render
    texture = SDL_CreateTexture(renderer,
                                pixformat,
                                SDL_TEXTUREACCESS_STREAMING,
                                width,
                                height);

    SDL_Rect rect;
    SDL_UpdateYUVTexture(texture, NULL,
                         vp->frame->data[0], vp->frame->linesize[0],
                         vp->frame->data[1], vp->frame->linesize[1],
                         vp->frame->data[2], vp->frame->linesize[2]);

    calculate_display_rect(&rect, left, top, width, height, vp->width, vp->height, vp->sar);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_RenderPresent(renderer);
}

int Player::realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
    Uint32 format;
    int access, w, h;
    if (!*texture || SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format)
    {
        void *pixels;
        int pitch;
        if (*texture)
            SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        if (init_texture)
        {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
        av_log(NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n", new_width, new_height, SDL_GetPixelFormatName(new_format));
    }
    return 0;
}

int Player::upload_texture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx)
{
    int ret = 0;
    Uint32 sdl_pix_fmt;
    SDL_BlendMode sdl_blendmode;
    get_sdl_pix_fmt_and_blendmode(frame->format, &sdl_pix_fmt, &sdl_blendmode);
    if (realloc_texture(tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt, frame->width, frame->height, sdl_blendmode, 0) < 0)
        return -1;
    switch (sdl_pix_fmt)
    {
    case SDL_PIXELFORMAT_UNKNOWN:
        /* This should only happen if we are not using avfilter... */
        *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
                                                frame->width, frame->height, (AVPixelFormat)(frame->format), frame->width, frame->height,
                                                AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
        if (*img_convert_ctx != NULL)
        {
            uint8_t *pixels[4];
            int pitch[4];
            if (!SDL_LockTexture(*tex, NULL, (void **)pixels, pitch))
            {
                sws_scale(*img_convert_ctx, (const uint8_t *const *)frame->data, frame->linesize,
                          0, frame->height, pixels, pitch);
                SDL_UnlockTexture(*tex);
            }
        }
        else
        {
            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            ret = -1;
        }
        break;
    case SDL_PIXELFORMAT_IYUV:
        if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0)
        {
            ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
                                       frame->data[1], frame->linesize[1],
                                       frame->data[2], frame->linesize[2]);
        }
        else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0)
        {
            ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0],
                                       frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                                       frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);
        }
        else
        {
            av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
            return -1;
        }
        break;
    default:
        if (frame->linesize[0] < 0)
        {
            ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
        }
        else
        {
            ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
        }
        break;
    }
    return ret;
}

void Player::video_open()
{
    int w, h;
    w = screen_width ? screen_width : default_width;
    h = screen_height ? screen_height : default_height;

    SDL_SetWindowTitle(window, state->filename);
    SDL_SetWindowSize(window, w, h);
    SDL_ShowWindow(window);

    width = w;
    height = h;
}

/*****************************************************/
/*                      public                       */
/*****************************************************/
int Player::open(const char *filename, const AVInputFormat *iformat)
{
    int ret = 0;
    SDL_Event event;
    state = new VideoState();

    ret = state->init(filename, iformat);
    if (ret < 0)
    {
        delete state;
        exit(1);
    }
    state->player = this;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }

    window = SDL_CreateWindow("Media Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, default_width, default_height, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        loge("Could not initiablize SDL window - %s\n", SDL_GetError());
        exit(1);
    }

    renderer = SDL_CreateRenderer(window, 0, -1);

    schedule_refresh(40);

    for (;;)
    {
        SDL_WaitEvent(&event);
        switch (event.type)
        {
        case FF_QUIT_EVENT:
        case SDL_QUIT:
            if (!quit)
            {
                quit = 1;
                close();
            }
            break;
        case FF_REFRESH_TIMER:
            video_refresh_timer(event.user.data1);
            break;
        default:
            break;
        }
        if (quit)
        {
            break;
        }
    }
    return 0;
}

void Player::set_default_window_size(int width, int height, AVRational sar)
{
    SDL_Rect rect;
    int max_width = screen_width ? screen_width : INT_MAX;
    int max_height = screen_height ? screen_height : INT_MAX;
    if (max_width == INT_MAX && max_height == INT_MAX)
    {
        max_height = height;
    }
    calculate_display_rect(&rect, 0, 0, max_width, max_height, width, height, sar);
    default_width = rect.w;
    default_height = rect.h;
}

void Player::close()
{
    if (quit)
    {
        delete state;
        state = NULL;
        if (renderer)
        {
            SDL_DestroyRenderer(renderer);
        }
        if (texture)
        {
            SDL_DestroyTexture(texture);
        }
        if (window)
        {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
}

int main(int argv, char **args)
{
    //设置av_log的level
    av_log_set_level(AV_LOG_INFO);
    char *input_filename = NULL;

    // input_filename = "/Users/rain/1.flv";

    //读取args
    if (argv < 2)
    {
        loge("should input play video file path.\n");
        exit(1);
    }

    input_filename = args[1];

    auto *player = new Player();
    player->open(input_filename, NULL);
    delete player;
    std::cout << "play over." << std::endl;
}