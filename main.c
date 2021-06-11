#include <stdio.h>
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <util.h>
#include <media_data_test.h>
#include "av_codecs.h"
#include "sdl_test.h"
#include "simple_player.h"

#ifndef AV_WB32
#define AV_WB32(p, val)                  \
    do                                   \
    {                                    \
        uint32_t d = (val);              \
        ((uint8_t *)(p))[3] = (d);       \
        ((uint8_t *)(p))[2] = (d) >> 8;  \
        ((uint8_t *)(p))[1] = (d) >> 16; \
        ((uint8_t *)(p))[0] = (d) >> 24; \
    } while (0)
#endif

#ifndef AV_RB16
#define AV_RB16(x)                      \
    ((((const uint8_t *)(x))[0] << 8) | \
     ((const uint8_t *)(x))[1])
#endif

// 使用ffmpeg中的api实现将目录中的文件列表简单打印出来
void ffmpeg_list(const char *dir_path)
{
    int ret;
    AVIODirContext *ctx = NULL;
    ret = avio_open_dir(&ctx, dir_path, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to open dir: %s\n", av_err2str(ret));
        return;
    }
    AVIODirEntry *entry = NULL;
    while (1)
    {
        ret = avio_read_dir(ctx, &entry);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to read dir: %s\n", av_err2str(ret));
            return;
        }
        if (!entry)
        {
            break;
        }

        av_log(NULL, AV_LOG_INFO, "%s\n", entry->name);
        avio_free_directory_entry(&entry);
    }

__fail:
    avio_close_dir(&ctx);
}

// 打印出流媒体文件的元数据信息
void ffmpeg_meta(const char *url)
{
    AVFormatContext *fmt_ctx = NULL;
    int ret;
    ret = avformat_open_input(&fmt_ctx, url, NULL, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to open url: %s, error:%s", url, av_err2str(ret));
        goto __fail;
    }
    av_dump_format(fmt_ctx, 0, url, 0);

__fail:
    avformat_close_input(&fmt_ctx);
}

// 从流媒体文件中提取出音频信息，自定义函数添加adts头部信息
void ffmpeg_audio_extract(const char *src, const char *dst)
{
    AVFormatContext *fmt_ctx = NULL;
    AVPacket pkt;
    AVFrame *frame = NULL;
    FILE *dst_fd = fopen(dst, "wb");
    int ret;
    int audio_index;
    int len;
    int aac_type, channels, sample_rate;
    if (!dst_fd)
    {
        loge("Failed to open dst file:%s", dst);
        return;
    }

    ret = avformat_open_input(&fmt_ctx, src, NULL, NULL);
    if (ret < 0)
    {
        loge("Failed to open url: %s, error:%s", src, av_err2str(ret));
        goto __fail;
    }

    aac_type = fmt_ctx->streams[1]->codecpar->profile;
    channels = fmt_ctx->streams[1]->codecpar->channels;
    sample_rate = fmt_ctx->streams[1]->codecpar->sample_rate;

    if (fmt_ctx->streams[1]->codecpar->codec_id != AV_CODEC_ID_AAC)
    {
        loge("the audio type is not AAC!\n");
        goto __fail;
    }
    else
    {
        logi("the audio type is AAC!\n");
    }

    av_dump_format(fmt_ctx, 0, src, 0);

    audio_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_index < 0)
    {
        loge("Failed to find the best stream.\n");
        goto __fail;
    }

    /*initialize packet*/
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    while (av_read_frame(fmt_ctx, &pkt) >= 0)
    {
        if (pkt.stream_index == audio_index)
        {
            //写入adts头部
            char adts_header_buf[ADTS_HEADER_LEN];
            adts_header(adts_header_buf, pkt.size, aac_type, sample_rate, channels);
            fwrite(adts_header_buf, 1, ADTS_HEADER_LEN, dst_fd);
            len = fwrite(pkt.data, 1, pkt.size, dst_fd);
            if (len != pkt.size)
            {
                logw("warning, length of data is not equal size of pkt.\n");
            }
        }
        av_packet_unref(&pkt);
    }

__fail:
    avformat_close_input(&fmt_ctx);
    if (dst_fd)
    {
        fclose(dst_fd);
    }
}

// 使用ffmpeg中的API实现audio的提取
void ffmpeg_audio_extract_api(const char *src, const char *dst)
{
    AVFormatContext *fmt_ctx = NULL;
    AVPacket pkt;

    AVFormatContext *out_fmt_ctx = NULL;
    AVOutputFormat *output_fmt = NULL;

    AVStream *in_stream = NULL;
    AVStream *out_stream = NULL;

    AVCodecParameters *in_codecpar = NULL;

    int error_code;
    int audio_stream_index = -1;

    //1, 打开目标文件写入流
    FILE *dst_fd = fopen(dst, "wb");
    if (!dst_fd)
    {
        loge("Failed to open dst file:%s", dst);
        return;
    }

    //2，打开源文件输入流
    error_code = avformat_open_input(&fmt_ctx, src, NULL, NULL);
    if (error_code < 0)
    {
        loge("Failed to open url: %s, error:%s", src, av_err2str(error_code));
        goto __fail;
    }

    //3，找到最佳的音频流索引值
    audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_index < 0)
    {
        loge("Failed to find the best stream.\n");
        goto __fail;
    }

    av_dump_format(fmt_ctx, 0, src, 0);

    //音频输入流
    in_stream = fmt_ctx->streams[1];
    in_codecpar = in_stream->codecpar;
    if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
    {
        loge("The codec type is invalid.\n");
        goto __fail;
    }

    //输出的格式上下文,一些准备工作，判断。。。。
    out_fmt_ctx = avformat_alloc_context();
    output_fmt = av_guess_format(NULL, dst, NULL);
    if (!output_fmt)
    {
        loge("Can't guess output format.\n");
        goto __fail;
    }
    out_fmt_ctx->oformat = output_fmt;

    out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!out_stream)
    {
        loge("Failed to create out stream.\n");
        goto __fail;
    }

    if (fmt_ctx->nb_streams < 2)
    {
        loge("The number of input stream is too less.\n");
        goto __fail;
    }

    if ((error_code = avcodec_parameters_copy(out_stream->codecpar, in_codecpar)) < 0)
    {
        loge("Failed to copy codec parameters: %s", av_err2str(error_code));
        goto __fail;
    }
    out_stream->codecpar->codec_tag = 0;
    if ((error_code = avio_open(&out_fmt_ctx->pb, dst, AVIO_FLAG_WRITE)) < 0)
    {
        loge("Can't open file %s, %s.\n", dst, av_err2str(error_code));
        goto __fail;
    }

    // 打印输出格式上下文的元数据信息
    av_dump_format(out_fmt_ctx, 0, dst, 1);

    //开始写入了
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (avformat_write_header(out_fmt_ctx, NULL) < 0)
    {
        loge("Error occurred when opening output file.\n");
        goto __fail;
    }

    while (av_read_frame(fmt_ctx, &pkt) >= 0)
    {
        if (pkt.stream_index == audio_stream_index)
        {
            pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            pkt.dts = pkt.pts;
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
            pkt.stream_index = 0;
            av_interleaved_write_frame(out_fmt_ctx, &pkt);
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(out_fmt_ctx);

__fail:
    avformat_close_input(&fmt_ctx);
    if (dst_fd)
    {
        fclose(dst_fd);
    }
    avio_close(out_fmt_ctx->pb);
}

static int alloc_and_copy(AVPacket *out,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size)
{
    uint32_t offset = out->size;
    uint8_t nal_header_size = 4;
    int err;

    err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);
    if (err < 0)
        return err;

    if (sps_pps)
        memcpy(out->data + offset, sps_pps, sps_pps_size);
    memcpy(out->data + sps_pps_size + nal_header_size + offset, in, in_size);
    if (!offset)
    {
        AV_WB32(out->data + sps_pps_size, 1);
    }
    else
    {
        (out->data + offset + sps_pps_size)[0] =
            (out->data + offset + sps_pps_size)[1] = 0;
        (out->data + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}

int h264_extradata_to_annexb(const uint8_t *codec_extradata, const int codec_extradata_size, AVPacket *out_extradata, int padding)
{
    uint16_t unit_size = 0;
    uint64_t total_size = 0;
    uint8_t *out = NULL;
    uint8_t unit_nb = 0;
    uint8_t sps_done = 0;
    uint8_t sps_seen = 0;
    uint8_t pps_seen = 0;
    uint8_t sps_offset = 0;
    uint8_t pps_offset = 0;

    /**
     * AVCC
     * bits
     *  8   version ( always 0x01 )
     *  8   avc profile ( sps[0][1] )
     *  8   avc compatibility ( sps[0][2] )
     *  8   avc level ( sps[0][3] )
     *  6   reserved ( all bits on )
     *  2   NALULengthSizeMinusOne    // 这个值是（前缀长度-1），值如果是3，那前缀就是4，因为4-1=3
     *  3   reserved ( all bits on )
     *  5   number of SPS NALUs (usually 1)
     *
     *  repeated once per SPS:
     *  16     SPS size
     *
     *  variable   SPS NALU data
     *  8   number of PPS NALUs (usually 1)
     *  repeated once per PPS
     *  16    PPS size
     *  variable PPS NALU data
     */

    const uint8_t *extradata = codec_extradata + 4;     //extradata存放数据的格式如上，前4个字节没用，所以将其舍弃
    static const uint8_t nalu_header[4] = {0, 0, 0, 1}; //每个H264裸数据都是以 0001 4个字节为开头的

    extradata++; //跳过一个字节，这个也没用

    sps_offset = pps_offset = -1;

    /* retrieve sps and pps unit(s) */
    unit_nb = *extradata++ & 0x1f; /* 取 SPS 个数，理论上可以有多个, 但我没有见到过多 SPS 的情况*/
    if (!unit_nb)
    {
        goto pps;
    }
    else
    {
        sps_offset = 0;
        sps_seen = 1;
    }

    while (unit_nb--)
    {
        int err;

        unit_size = AV_RB16(extradata);
        total_size += unit_size + 4; //加上4字节的h264 header, 即 0001
        if (total_size > INT_MAX - padding)
        {
            av_log(NULL, AV_LOG_ERROR,
                   "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }

        //2:表示上面 unit_size 的所占字结数
        //这句的意思是 extradata 所指的地址，加两个字节，再加 unit 的大小所指向的地址
        //是否超过了能访问的有效地址空间
        if (extradata + 2 + unit_size > codec_extradata + codec_extradata_size)
        {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                                       "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }

        //分配存放 SPS 的空间
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;

        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);
        extradata += 2 + unit_size;
    pps:
        //当 SPS 处理完后，开始处理 PPS
        if (!unit_nb && !sps_done++)
        {
            unit_nb = *extradata++; /* number of pps unit(s) */
            if (unit_nb)
            {
                pps_offset = total_size;
                pps_seen = 1;
            }
        }
    }

    //余下的空间清0
    if (out)
    {
        memset(out + total_size, 0, padding);
    }

    if (!sps_seen)
        av_log(NULL, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    if (!pps_seen)
        av_log(NULL, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    out_extradata->data = out;
    out_extradata->size = total_size;

    return 0;
}

int h264_mp4toannexb(AVFormatContext *fmt_ctx, AVPacket *in, FILE *dst_fd)
{

    AVPacket *out = NULL;
    AVPacket spspps_pkt;

    int len;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size = 0;
    const uint8_t *buf;
    const uint8_t *buf_end;
    int buf_size;
    int ret = 0, i;

    out = av_packet_alloc();

    buf = in->data;
    buf_size = in->size;
    buf_end = in->data + in->size;

    do
    {
        ret = AVERROR(EINVAL);
        //因为每个视频帧的前 4 个字节是视频帧的长度
        //如果buf中的数据都不能满足4字节，所以后面就没有必要再进行处理了
        if (buf + 4 > buf_end)
            goto fail;

        //将前四字节转换成整型,也就是取出视频帧长度
        for (nal_size = 0, i = 0; i < 4; i++)
            nal_size = (nal_size << 8) | buf[i];

        buf += 4;                //跳过4字节（也就是视频帧长度），从而指向真正的视频帧数据
        unit_type = *buf & 0x1f; //视频帧的第一个字节里有NAL TYPE

        //如果视频帧长度大于从 AVPacket 中读到的数据大小，说明这个数据包肯定是出错了
        if (nal_size > buf_end - buf || nal_size < 0)
            goto fail;

        /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
        if (unit_type == 5)
        {

            //在每个I帧之前都加 SPS/PPS
            h264_extradata_to_annexb(fmt_ctx->streams[in->stream_index]->codecpar->extradata,
                                     fmt_ctx->streams[in->stream_index]->codecpar->extradata_size,
                                     &spspps_pkt,
                                     AV_INPUT_BUFFER_PADDING_SIZE);

            if ((ret = alloc_and_copy(out,
                                      spspps_pkt.data, spspps_pkt.size,
                                      buf, nal_size)) < 0)
                goto fail;
        }
        else
        {
            if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                goto fail;
        }

        len = fwrite(out->data, 1, out->size, dst_fd);
        if (len != out->size)
        {
            av_log(NULL, AV_LOG_DEBUG, "warning, length of writed data isn't equal pkt.size(%d, %d)\n",
                   len,
                   out->size);
        }
        fflush(dst_fd);

    next_nal:
        buf += nal_size;
        cumul_size += nal_size + 4; //s->length_size;
    } while (cumul_size < buf_size);

fail:
    av_packet_free(&out);

    return ret;
}

void ffmpeg_video_extract(const char *src, const char *dst)
{
    AVFormatContext *fmt_ctx = NULL;
    AVPacket pkt;
    AVFrame *frame = NULL;
    int error_code;         //错误码
    int video_stream_index; //视频流的索引值
    //1,打开目标文件写入流
    FILE *dst_fd = fopen(dst, "wb");
    if (!dst_fd)
    {
        loge("Failed to open dst file:%s\n", dst);
        return;
    }

    //2，打开源文件读取，并且记录其AVFormatContext
    error_code = avformat_open_input(&fmt_ctx, src, NULL, NULL);
    if (error_code < 0)
    {
        loge("Failed to open src file:%s, error:%s", src, av_err2str(error_code));
        goto __ERROR;
    }

    //3, 打印源文件的元数据信息
    ffmpeg_meta(src);

    //4,一个包一个包的读取信息, 初始化
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    frame = av_frame_alloc();

    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0)
    {
        loge("Failed to find the best video stream.\n");
        goto __ERROR;
    }

    while (av_read_frame(fmt_ctx, &pkt) >= 0)
    {
        if (pkt.stream_index == video_stream_index)
        {
            //5， 将读取到的流信息，写入到目标文件中
            h264_mp4toannexb(fmt_ctx, &pkt, dst_fd);
        }
        //释放packet引用，防止内存泄漏
        av_packet_unref(&pkt);
    }

__ERROR:
    avformat_close_input(&fmt_ctx);
    if (dst_fd)
    {
        fclose(dst_fd);
    }
}

void ffmpeg_video_extract_api(const char *src, const char *dst)
{
    AVFormatContext *fmt_ctx = NULL;
    AVPacket pkt;
    AVFrame *frame = NULL;
    int error_code;         //错误码
    int video_stream_index; //视频流的索引值
    int ret;

    //out相关
    AVBSFContext *h264bsf_ctx;
    const AVBitStreamFilter *filter = av_bsf_get_by_name("h264_mp4toannexb");

    if ((ret = av_bsf_alloc(filter, &h264bsf_ctx)) < 0)
    {
        loge("Failed to alloc bsf context.\n");
        return;
    }

    AVPacket *out_ptk = av_packet_alloc();

    //1,打开目标文件写入流
    FILE *dst_fd = fopen(dst, "wb");
    if (!dst_fd)
    {
        loge("Failed to open dst file:%s\n", dst);
        return;
    }

    //2，打开源文件读取，并且记录其AVFormatContext
    error_code = avformat_open_input(&fmt_ctx, src, NULL, NULL);
    if (error_code < 0)
    {
        loge("Failed to open src file:%s, error:%s", src, av_err2str(error_code));
        goto __ERROR;
    }

    //3, 打印源文件的元数据信息
    ffmpeg_meta(src);

    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0)
    {
        loge("Failed to find the best video stream.\n");
        goto __ERROR;
    }

    avcodec_parameters_copy(h264bsf_ctx->par_in, fmt_ctx->streams[video_stream_index]->codecpar);
    av_bsf_init(h264bsf_ctx);

    while (av_read_frame(fmt_ctx, out_ptk) >= 0)
    {
        if (out_ptk->stream_index == video_stream_index)
        {
            //5， 将读取到的流信息，写入到目标文件中
            ret = av_bsf_send_packet(h264bsf_ctx, out_ptk);
            if (ret < 0)
            {
                loge("error: av_bsf_send_packet %s", av_err2str(ret));
            }
            while ((ret = av_bsf_receive_packet(h264bsf_ctx, out_ptk)) == 0)
            {
                fwrite(out_ptk->data, out_ptk->size, 1, dst_fd);
            }
        }
        //释放packet引用，防止内存泄漏
        av_packet_unref(out_ptk);
    }

__ERROR:
    avformat_close_input(&fmt_ctx);
    if (dst_fd)
    {
        fclose(dst_fd);
    }
    av_packet_free(&out_ptk);
    av_bsf_free(&h264bsf_ctx);
}

void ffmpeg_mp4_to_flv(const char *src, const char *dst)
{
    AVFormatContext *i_fmt_ctx = NULL, *o_fmt_ctx = NULL;
    AVOutputFormat *ofmt;
    AVPacket pkt;
    int ret;
    int stream_mapping_size;
    int *stream_mapping = NULL;
    int stream_index = 0;

    //1, 打开输入文件，并且记录下输入格式的上下文
    if ((ret = avformat_open_input(&i_fmt_ctx, src, 0, 0)) < 0)
    {
        loge("Failed to open src file: %s, error: %s\n", src, av_err2str(ret));
        return;
    }

    //2, 检查文件中的流信息
    if ((ret = avformat_find_stream_info(i_fmt_ctx, 0)) < 0)
    {
        loge("Failed to retrieve input stream info.\n");
        goto end;
    }

    //3，打印输入文件格式信息
    av_dump_format(i_fmt_ctx, 0, src, 0);

    //4，给输出文件分配上下文
    avformat_alloc_output_context2(&o_fmt_ctx, NULL, NULL, dst);
    if (!o_fmt_ctx)
    {
        loge("Could not create output context.\n");
        goto end;
    }

    //5, 从此开始，就要将输入文件中需要输出的流，写入到输出文件中了
    stream_mapping_size = i_fmt_ctx->nb_streams;
    stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping)
    {
        loge("error: stream mapping.\n");
        goto end;
    }

    ofmt = o_fmt_ctx->oformat;

    // 6, 遍历所有的流，将所需要的流信息写入到输出文件中
    for (int i = 0; i < i_fmt_ctx->nb_streams; i++)
    {
        //6.1，输入流和输出流
        AVStream *in_stream = i_fmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(o_fmt_ctx, NULL);

        //6.2， codec的参数
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) //只保存映射音频，视频，字幕这三种流，其他流舍弃
        {
            stream_mapping[i] = -1;
            continue;
        }

        stream_mapping[i] = stream_index++;
        if (!out_stream)
        {
            loge("Failed to allocating output stream.\n");
            goto end;
        }

        //6.3， 复制参数到输出流中
        if ((ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar)) < 0)
        {
            loge("Failed to copy codec params.\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    //7, 打印输出的上下文格式
    av_dump_format(o_fmt_ctx, 0, dst, 1);

    //8，检查输出文件能不能打开
    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&o_fmt_ctx->pb, dst, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            loge("Failed to open output file: %s\n", dst);
            goto end;
        }
    }

    //9，写入header
    if ((ret = avformat_write_header(o_fmt_ctx, NULL)) < 0)
    {
        loge("Failed to write header to output file.\n");
        goto end;
    }

    //10, 把每一帧都写入
    while (1)
    {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(i_fmt_ctx, &pkt);
        if (ret < 0)
        {
            break;
        }
        in_stream = i_fmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size || stream_mapping[pkt.stream_index] < 0)
        {
            av_packet_unref(&pkt);
            continue;
        }
        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = o_fmt_ctx->streams[pkt.stream_index];

        //10.1 copy packet
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        ret = av_interleaved_write_frame(o_fmt_ctx, &pkt);
        if (ret < 0)
        {
            loge("Error occuring while write frame.\n");
            break;
        }
        av_packet_unref(&pkt);
    }
    av_write_trailer(o_fmt_ctx);

end:
    avformat_close_input(&i_fmt_ctx);
    /* close output */
    if (o_fmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&o_fmt_ctx->pb);
    avformat_free_context(o_fmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF)
    {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return;
    }
}

void ffmpeg_cut_video(int start_second, int end_second, const char *src, const char *dst)
{
    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ofmt_ctx;
    AVOutputFormat *ofmt;
    AVPacket pkt;
    int ret;

    if ((ret = avformat_open_input(&ifmt_ctx, src, NULL, NULL)) < 0)
    {
        loge("Failed to open src file: %s, error: %s\n", dst, av_err2str(ret));
        return;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0)
    {
        loge("Failed to retrieve input stream info.\n");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, src, 0);

    ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, dst);
    if (ret < 0)
    {
        loge("Failed to alloc output context for %s, error: %s", dst, av_err2str(ret));
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    for (int i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream)
        {
            loge("Failed to new output stream.\n");
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (ret < 0)
        {
            loge("Failed to copy params. error: %s", av_err2str(ret));
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    av_dump_format(ofmt_ctx, 0, dst, 1);

    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb, dst, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            loge("Failed to open output file: %s\n", dst);
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        loge("Faield to write header.\n");
        goto end;
    }

    //这里开始，正式裁剪视频
    ret = av_seek_frame(ifmt_ctx, -1, start_second * AV_TIME_BASE, AVSEEK_FLAG_ANY); //seek到起始时间
    if (ret < 0)
    {
        loge("Error seek.\n");
        goto end;
    }

    int64_t *dts_start_from = malloc(sizeof(int64_t) * ifmt_ctx->nb_streams);
    memset(dts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);
    int64_t *pts_start_from = malloc(sizeof(int64_t) * ifmt_ctx->nb_streams);
    memset(pts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);

    while (1)
    {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
        {
            break;
        }
        in_stream = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        //判断时间是否超过了终止时间
        if (av_q2d(in_stream->time_base) * pkt.pts > end_second)
        {
            av_packet_unref(&pkt);
            break;
        }

        if (dts_start_from[pkt.stream_index] == 0)
        {
            dts_start_from[pkt.stream_index] = pkt.dts;
        }

        if (pts_start_from[pkt.stream_index] == 0)
        {
            pts_start_from[pkt.stream_index] = pkt.pts;
        }

        //copy packet
        pkt.dts = av_rescale_q_rnd(pkt.dts - dts_start_from[pkt.stream_index], in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.pts = av_rescale_q_rnd(pkt.pts - pts_start_from[pkt.stream_index], in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        if (pkt.dts < 0)
        {
            pkt.dts = 0;
        }
        if (pkt.pts < 0)
        {
            pkt.pts = 0;
        }

        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0)
        {
            loge("Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    free(dts_start_from);
    free(pts_start_from);
    av_write_trailer(ofmt_ctx);

end:
    avformat_close_input(&ifmt_ctx);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF)
    {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return;
    }
}

int main(int argv, char **args)
{
    // av_log_set_level(AV_LOG_INFO);

    // char *src = NULL;
    // char *dst = NULL;
    // int from = 0;
    // int end = 0;
    // if (argv < 3)
    // {
    //     loge("the num of input params should be more than three.\n");
    //     return -1;
    // }
    // if (argv == 5)
    // {
    //     from = atoi(args[3]);
    //     end = atoi(args[4]);
    //     src = args[1];
    //     dst = args[2];
    // }
    // else
    // {
    //     src = args[1];
    //     dst = args[2];
    // }

    // if (!src || !dst)
    // {
    //     loge("src or dst be null.\n");
    //     return -1;
    // }
    // // ffmpeg_cut_video(from, end, src, dst);
    // decode_audio(src, dst);

    // char *src = NULL;
    // if (argv < 2)
    // {
    //     printf("the num of input is too less.\n");
    //     return -1;
    // }

    // src = args[1];
    // simplest_h264_parser(src);
    // return 0;
    // hello_sdl2();
    // play_yuv("/users/rain/Downloads/a.yuv", 448, 960);
    // simple_play("/users/rain/Documents/SampleVideo_1280x720_10mb.mp4");
    play_pcm("/users/rain/Documents/out.pcm");
}
