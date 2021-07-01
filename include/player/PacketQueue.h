#ifndef _PACKET_QUEUE_H
#define _PACKET_QUEUE_H

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <SDL2/SDL.h>
#include "util.h"
}

class MyAVPacketList
{
public:
    AVPacket *pkt;
    MyAVPacketList()
    {
    }
    int init()
    {
        pkt = av_packet_alloc();
        if (pkt)
        {
            return 0;
        }
        else
        {
            logf("MyAVPacketList::init(): Failed to alloc packet.\n");
            return -1;
        }
    }
    ~MyAVPacketList()
    {
        av_packet_free(&pkt);
    }
};

class PacketQueue
{
private:
    /* data */
    AVFifoBuffer *packet_list;
    int nb_packets;
    int size = 0;

    int64_t duration = 0;
    int abort_request;
    SDL_mutex *mutex;
    SDL_cond *cond;

    int put_internal(AVPacket *);

public:
    PacketQueue();
    int init();
    void start();
    void abort();
    void destory();
    MyAVPacketList *get(int);
    int put(AVPacket *);
    /**
     * 将队列中的packet全部读取出来
     * */
    void flush(); 
    int isAbort();
    ~PacketQueue();
};

#endif