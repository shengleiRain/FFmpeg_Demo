#include "av_codecs.h"
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include "util.h"

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096
#define MAX_AUDIO_FRAME_SIZE 192000

void write_frame_to_file(AVCodecContext *codec_ctx, AVFrame *convert_frame, FILE *dst_fp)
{
    fwrite(convert_frame->data[0], 1, codec_ctx->width * codec_ctx->height, dst_fp);
    fwrite(convert_frame->data[1], 1, codec_ctx->width * codec_ctx->height / 4, dst_fp);
    fwrite(convert_frame->data[2], 1, codec_ctx->width * codec_ctx->height / 4, dst_fp);
}
void decode_video(const char *src, const char *dst)
{
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

    //1, 首先打开输入文件的格式上下文，并记录
    ret = avformat_open_input(&ifmt_ctx, src, NULL, NULL);
    if (ret < 0)
    {
        loge("Failed to open input file: %s, error: %s\n", src, av_err2str(ret));
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
        loge("Failed to find decoder for %s.\n", src);
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

    av_dump_format(ifmt_ctx, 0, src, 0); //打印格式信息

    dst_fp = fopen(dst, "wb+");
    if (!dst_fp)
    {
        loge("Could not open dst file.\n");
        goto error;
    }

    img_convert_ctx = sws_getContext(codec_ctx->width,
                                     codec_ctx->height,
                                     codec_ctx->pix_fmt,
                                     codec_ctx->width,
                                     codec_ctx->height,
                                     AV_PIX_FMT_YUV420P,
                                     SWS_BICUBIC, NULL, NULL, NULL);
    //7, 循环读取packet
    while ((av_read_frame(ifmt_ctx, pkt)) >= 0)
    {
        if (pkt->stream_index == video_index)
        {
            //7.1, 将packet解码成frame
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
                write_frame_to_file(codec_ctx, yuvFrame, dst_fp);
            }
        }
        av_packet_unref(pkt);
    }

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

void decode_audio(const char *src, const char *dst)
{
    AVFormatContext *ifmt_ctx = NULL;
    int ret;
    int audio_index;
    AVCodecContext *codec_ctx = NULL;
    AVCodecParameters *codecpar = NULL;
    AVCodec *codec;
    AVPacket *pkt;
    AVFrame *frame;
    AVFrame *yuvFrame;
    int got_picture;
    int frame_cnt = 0;
    FILE *dst_fp = NULL;
    struct SwrContext *audio_convert_ctx;
    uint8_t *out_buffer;

    //1, 首先打开输入文件的格式上下文，并记录
    ret = avformat_open_input(&ifmt_ctx, src, NULL, NULL);
    if (ret < 0)
    {
        loge("Failed to open input file: %s, error: %s\n", src, av_err2str(ret));
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
    audio_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_index < 0)
    {
        loge("Failed to find best stream for audio.\n");
        goto error;
    }

    codecpar = ifmt_ctx->streams[audio_index]->codecpar;

    //4, 根据id,找到音频所使用的解码器
    codec = avcodec_find_decoder(codecpar->codec_id);
    logi("use codec name is %s\n", codec->long_name);
    if (!codec)
    {
        loge("Failed to find decoder for %s.\n", src);
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
    pkt = av_packet_alloc();

    av_dump_format(ifmt_ctx, 0, src, 0); //打印格式信息

    dst_fp = fopen(dst, "wb+");
    if (!dst_fp)
    {
        loge("Could not open dst file.\n");
        goto error;
    }

    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    logi("codec frame size=%d\n", codec_ctx->frame_size);
    //设置输出的音频采样信息的格式信息
    int out_nb_samples = codec_ctx->frame_size;
    int out_sample_rate = 44100; //采样率
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, AV_SAMPLE_FMT_S16, 1);
    out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);

    logi("out_buffer_size=%d\n", out_buffer_size);

    int in_channel_layout = av_get_default_channel_layout(codec_ctx->channels);

    audio_convert_ctx = swr_alloc();
    audio_convert_ctx = swr_alloc_set_opts(audio_convert_ctx,
                                           out_channel_layout,
                                           AV_SAMPLE_FMT_S16,
                                           out_sample_rate,
                                           in_channel_layout,
                                           codec_ctx->sample_fmt,
                                           codec_ctx->sample_rate,
                                           0, NULL);
    swr_init(audio_convert_ctx);

    //7, 循环读取packet
    while ((av_read_frame(ifmt_ctx, pkt)) >= 0)
    {
        if (pkt->stream_index == audio_index)
        {
            //7.1, 将packet解码成frame
            ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0)
            {
                loge("Failed to send packet. error:%s", av_err2str(ret));
            }
            while ((avcodec_receive_frame(codec_ctx, frame)) == 0)
            {
                frame_cnt++;
                // logi("got picture %d.\n", frame_cnt);
                int data_size = av_get_bytes_per_sample(codec_ctx->sample_fmt);
                if (data_size < 0)
                {
                    loge("Failed to calculate data size.\n");
                    goto error;
                }
                swr_convert(audio_convert_ctx,
                            &out_buffer,
                            MAX_AUDIO_FRAME_SIZE,
                            (const uint8_t **)frame->data,
                            frame->nb_samples);
                fwrite(out_buffer, 1, out_buffer_size, dst_fp);
            }
        }
        av_packet_unref(pkt);
    }

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