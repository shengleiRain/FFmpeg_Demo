#include "Decoder.h"

Decoder::Decoder(/* args */)
{
}

Decoder::~Decoder()
{
    abort();
    destory();
}

int Decoder::init(AVCodecContext *codec_ctx, PacketQueue *pkt_queue, FrameQueue *frame_queue, SDL_cond *empty_queue_cond)
{
    pkt = av_packet_alloc();
    if (!pkt)
    {
        return AVERROR(ENOMEM);
    }
    this->avctx = codec_ctx;
    this->pkt_queue = pkt_queue;
    this->frame_queue = frame_queue;
    this->empty_queue_cond = empty_queue_cond;
    this->start_pts = AV_NOPTS_VALUE;
    return 0;
}

int Decoder::start(int (*fn)(void *), const char *thread_name, void *arg)
{
    pkt_queue->start(); //将packet queue开启
    decoder_tid = SDL_CreateThread(fn, thread_name, arg);
    if (!decoder_tid)
    {
        loge("Decoder::start:: SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}

void Decoder::abort()
{
    pkt_queue->abort();    //终止packet queue队列
    frame_queue->signal(); //唤醒帧队列，以便队列中的frame能够显示出来
    SDL_WaitThread(decoder_tid, NULL);
    decoder_tid = NULL;
    pkt_queue->flush(); //清空队列
}

int Decoder::decode_frame(AVFrame *frame)
{
    int ret = AVERROR(EAGAIN); //当前状态不对，读取的帧不行 output is not available in this state - user must try to send new input
    for (;;)
    {
        do
        {
            //检查退出位
            if (pkt_queue->isAbort())
            {
                return -1;
            }
            switch (avctx->codec_type)
            {
            case AVMEDIA_TYPE_VIDEO:
                ret = avcodec_receive_frame(avctx, frame);
                break;
            case AVMEDIA_TYPE_AUDIO:
                ret = avcodec_receive_frame(avctx, frame);
                if (ret >= 0)
                {
                    AVRational tb = (AVRational){1, frame->sample_rate};
                    if (frame->pts != AV_NOPTS_VALUE)
                        frame->pts = av_rescale_q(frame->pts, avctx->pkt_timebase, tb);
                    else if (next_pts != AV_NOPTS_VALUE)
                        frame->pts = av_rescale_q(next_pts, next_pts_tb, tb);
                    if (frame->pts != AV_NOPTS_VALUE)
                    {
                        next_pts = frame->pts + frame->nb_samples;
                        next_pts_tb = tb;
                    }
                }
                break;
            }
            //读取到末尾了
            if (ret == AVERROR_EOF)
            {
                finished = 1;
                avcodec_flush_buffers(avctx);
                return 0;
            }
            if (ret >= 0)
            {
                return 1;
            }
        } while (ret != AVERROR(EAGAIN)); //这个循环，从codec_ctx中读取出可用的frame

        if (packet_pending)
        {
            packet_pending = 0;
        }
        else
        {
            auto *packetList = pkt_queue->get(1);
            if (!packetList)
            {
                return -1;
            }
            else
            {
                av_packet_move_ref(pkt, packetList->pkt); //使用pkt来暂存读取到的packet数据
                delete packetList;
            }
        }

        if (avcodec_send_packet(avctx, pkt) == AVERROR(EAGAIN)) //证明avctx中还有frame可以读取
        {
            loge("Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
            packet_pending = 1;
        }
        else
        {
            av_packet_unref(pkt);
        }
    }
}

void Decoder::destory()
{
    av_packet_free(&pkt);
    avcodec_free_context(&avctx);
}