#ifndef _DECODER_H
#define _DECODER_H

extern "C"
{
#include <libavformat/avformat.h>
#include "util.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
}

class Decoder
{
private:
    AVPacket *pkt;
    PacketQueue *pkt_queue;
    FrameQueue * frame_queue;
    AVCodecContext *avctx;
    int packet_pending = 0; //avctx中是否还有frame剩余，如有则不需要从pkt_queue中读取
    int finished = 0;
    SDL_cond *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    SDL_Thread *decoder_tid;

public:
    Decoder(/* args */);
    int init(AVCodecContext *codec_ctx, PacketQueue *pkt_queue, FrameQueue *frame_queue, SDL_cond *empty_queue_cond);
    /**
     * 开启解码线程
     * */
    int start(int (*fn)(void *), const char *thread_name, void* arg);
    /**
     * 终止解码线程
     * */
    void abort();
    void destory();
    int decode_frame(AVFrame *frame);
    ~Decoder();
};

#endif