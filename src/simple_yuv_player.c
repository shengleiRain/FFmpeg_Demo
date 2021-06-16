#include "simple_yuv_player.h"
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include "util.h"
#include <SDL2/SDL.h>
#define REFRESH_VIDEO_EVENT (SDL_USEREVENT + 1)
#define BREAK_EVENT (SDL_USEREVENT + 2)

int refresh_player(int *thread_exit)
{
    *thread_exit = 0;
    while (*thread_exit == 0)
    {
        SDL_Event event;
        event.type = REFRESH_VIDEO_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(40);
    }
    *thread_exit = 0;
    //break
    SDL_Event event;
    event.type = BREAK_EVENT;
    SDL_PushEvent(&event);
    return 0;
}

void simple_play(const char *src_file)
{
    /*=================FFmpeg=====================*/
    AVFormatContext *ifmt_ctx = NULL;
    int ret;
    int video_index;
    AVCodecContext *codec_ctx = NULL;
    AVCodecParameters *codecpar = NULL;
    AVCodec *codec;
    AVPacket *pkt;
    AVFrame *frame;
    AVFrame *yuvFrame;
    int got_picture;
    int frame_cnt = 0;
    FILE *dst_fp = NULL;
    struct SwsContext *img_convert_ctx;
    uint8_t *out_buffer;

    /*=================SDL==========================*/
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    int quit = 0;
    SDL_Event event;
    SDL_Init(SDL_INIT_VIDEO);
    int screen_w, screen_h;

    //1, 首先打开输入文件的格式上下文，并记录
    ret = avformat_open_input(&ifmt_ctx, src_file, NULL, NULL);
    if (ret < 0)
    {
        loge("Failed to open input file: %s, error: %s\n", src_file, av_err2str(ret));
        return;
    }

    //2, 查找流信息
    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    if (ret < 0)
    {
        loge("Faield to find stream info.\n");
        goto error;
    }

    //3, 找到video最合适的流所对应的索引值
    video_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_index < 0)
    {
        loge("Failed to find best stream for video.\n");
        goto error;
    }

    codecpar = ifmt_ctx->streams[video_index]->codecpar;

    //4, 根据id,找到视频所使用的解码器
    codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec)
    {
        loge("Failed to find decoder for %s.\n", src_file);
        goto error;
    }

    //5, 根据codecpar中的信息，生成codec_ctx上下文信息
    codec_ctx = avcodec_alloc_context3(NULL);
    if (!codec_ctx)
    {
        loge("Failed to alloc codec context.\n");
        goto error;
    }

    ret = avcodec_parameters_to_context(codec_ctx, codecpar);
    if (ret < 0)
    {
        loge("avcodec context copy failed.\n");
        goto error;
    }

    //6，打开decoder
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0)
    {
        loge("Failed to open codec.\n");
        goto error;
    }

    frame = av_frame_alloc();
    yuvFrame = av_frame_alloc();
    pkt = av_packet_alloc();
    out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height));
    avpicture_fill((AVPicture *)yuvFrame, out_buffer, AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height);

    av_dump_format(ifmt_ctx, 0, src_file, 0); //打印格式信息

    img_convert_ctx = sws_getContext(codec_ctx->width,
                                     codec_ctx->height,
                                     codec_ctx->pix_fmt,
                                     codec_ctx->width,
                                     codec_ctx->height,
                                     AV_PIX_FMT_YUV420P,
                                     SWS_BICUBIC, NULL, NULL, NULL);

    screen_h = codec_ctx->height;
    screen_w = codec_ctx->width;

    //实现SDL相关代码
    window = SDL_CreateWindow(src_file, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, codec_ctx->width, codec_ctx->height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Log("Failed to create window.\n");
        goto __EXIT;
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
    {
        SDL_Log("Failed to create renderer.\n");
        goto __WINDOW;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, codec_ctx->width, codec_ctx->height);
    if (!texture)
    {
        SDL_Log("Failed to create texture. error: %s\n", SDL_GetError());
        goto __RENDERER;
    }

    SDL_Rect rect;
    SDL_Thread *thread = SDL_CreateThread(refresh_player, NULL, &quit);

    for (;;)
    {
        SDL_WaitEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            SDL_Log("quit window size=%d*%d", screen_w, screen_h);
            quit = 1;
            break;
        case REFRESH_VIDEO_EVENT:
            SDL_Log("refresh video window size=%d*%d", screen_w, screen_h);

            //需要读取到此时的第一个视频packet
            while (av_read_frame(ifmt_ctx, pkt) >= 0 && pkt->stream_index != video_index)
            {
                av_packet_unref(pkt);
            }
            if (pkt->stream_index == video_index)
            {
                ret = avcodec_send_packet(codec_ctx, pkt);
                if (ret < 0)
                {
                    loge("Failed to send packet. error:%s", av_err2str(ret));
                }
                while ((avcodec_receive_frame(codec_ctx, frame)) == 0)
                {
                    frame_cnt++;
                    logi("got picture %d.\n", frame_cnt);
                    sws_scale(img_convert_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, codec_ctx->height,
                                yuvFrame->data, yuvFrame->linesize);
                    SDL_UpdateTexture(texture, NULL, yuvFrame->data[0], yuvFrame->linesize[0]);

                    rect.x = 0;
                    rect.y = 0;
                    rect.w = screen_w;
                    rect.h = screen_h;

                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, &rect);
                    SDL_RenderPresent(renderer);
                }
            }
            av_packet_unref(pkt);
            break;
        case SDL_WINDOWEVENT:
            SDL_Log("window size=%d*%d", screen_w, screen_h);
            SDL_GetWindowSize(window, &screen_w, &screen_h);
            break;
        case BREAK_EVENT:
            SDL_Log("break window size=%d*%d", screen_w, screen_h);
            goto __RENDERER;
            break;
        default:
            break;
        }

        // //7, 循环读取packet
        // while ((av_read_frame(ifmt_ctx, pkt)) >= 0)
        // {
        //     if (pkt->stream_index == video_index)
        //     {
        //         //7.1, 将packet解码成frame
        //         ret = avcodec_send_packet(codec_ctx, pkt);
        //         if (ret < 0)
        //         {
        //             loge("Failed to send packet. error:%s", av_err2str(ret));
        //         }
        //         while ((avcodec_receive_frame(codec_ctx, frame)) == 0)
        //         {
        //             frame_cnt++;
        //             logi("got picture %d.\n", frame_cnt);
        //             sws_scale(img_convert_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, codec_ctx->height,
        //                       yuvFrame->data, yuvFrame->linesize);

        //             SDL_WaitEvent(&event);
        //             switch (event.type)
        //             {
        //             case SDL_QUIT:
        //                 SDL_Log("quit window size=%d*%d", screen_w, screen_h);
        //                 quit = 1;
        //                 break;
        //             case REFRESH_VIDEO_EVENT:
        //                 SDL_Log("refresh video window size=%d*%d", screen_w, screen_h);

        //                 SDL_UpdateTexture(texture, NULL, yuvFrame->data[0], yuvFrame->linesize[0]);

        //                 rect.x = 0;
        //                 rect.y = 0;
        //                 rect.w = screen_w;
        //                 rect.h = screen_h;

        //                 SDL_RenderClear(renderer);
        //                 SDL_RenderCopy(renderer, texture, NULL, &rect);
        //                 SDL_RenderPresent(renderer);
        //                 break;
        //             case SDL_WINDOWEVENT:
        //                 SDL_Log("window size=%d*%d", screen_w, screen_h);
        //                 SDL_GetWindowSize(window, &screen_w, &screen_h);
        //                 break;
        //             case BREAK_EVENT:
        //                 SDL_Log("break window size=%d*%d", screen_w, screen_h);
        //                 goto __RENDERER;
        //                 break;
        //             default:
        //                 break;
        //             }
        //         }
        //     }
        // av_packet_unref(pkt);
    }

__RENDERER:
    SDL_DestroyRenderer(renderer);
__WINDOW:
    SDL_DestroyWindow(window);
__EXIT:
    SDL_Quit();
error:
    avformat_close_input(&ifmt_ctx);
    if (!frame)
    {
        av_frame_free(&frame);
    }
    if (!yuvFrame)
    {
        av_frame_free(&yuvFrame);
    }
    if (!codec_ctx)
    {
        avcodec_free_context(&codec_ctx);
    }
    if (!dst_fp)
    {
        fclose(dst_fp);
    }
    av_free(out_buffer);
}