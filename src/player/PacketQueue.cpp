#include "PacketQueue.h"
#include <iostream>

PacketQueue::PacketQueue()
{
    std::cout << "init packet queue" << std::endl;
}

int PacketQueue::init()
{
    mutex = SDL_CreateMutex();
    if (!mutex)
    {
        logf("SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    cond = SDL_CreateCond();
    if (!cond)
    {
        logf("SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    packet_list = av_fifo_alloc(sizeof(MyAVPacketList));
    if (!packet_list)
    {
        return AVERROR(ENOMEM);
    }
    abort_request = 1;
    duration = 0;
    size = 0;
    nb_packets = 0;
    return 0;
}

MyAVPacketList *PacketQueue::get(int block)
{
    MyAVPacketList *temp = new MyAVPacketList();
    temp->init();
    int ret = 0;

    SDL_LockMutex(mutex); //加锁

    for (;;)
    {
        if (abort_request)
        {
            ret = -1;
            break;
        }

        //检查buffer的空间
        if (av_fifo_size(packet_list) >= sizeof(MyAVPacketList))
        {
            av_fifo_generic_read(packet_list, temp, sizeof(MyAVPacketList), NULL);
            nb_packets--;
            size -= temp->pkt->size + sizeof(MyAVPacketList);
            duration -= temp->pkt->duration;
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(cond, mutex);
        }
    }
    SDL_UnlockMutex(mutex); //解锁
    if (ret <= 0)
    {
        delete temp;
        return NULL;
    }
    else
        return temp;
}

void PacketQueue::flush()
{
    MyAVPacketList temp;
    temp.init();
    SDL_LockMutex(mutex);
    while (av_fifo_size(packet_list) >= sizeof(MyAVPacketList))
    {
        av_fifo_generic_read(packet_list, &temp, sizeof(MyAVPacketList), NULL);
        av_packet_free(&temp.pkt);
    }
    nb_packets = 0;
    size = 0;
    duration = 0;

    SDL_UnlockMutex(mutex);
}

int PacketQueue::put_internal(AVPacket *pkt)
{
    MyAVPacketList *temp = new MyAVPacketList();
    int ret = 0;
    ret = temp->init();
    if (ret < 0)
    {
        goto fail;
    }
    if (abort_request)
    {
        ret = -1;
        goto fail;
    }
    
    //检查buffer中的空间是否足够，不够就增加空间
    if (av_fifo_space(packet_list) < sizeof(MyAVPacketList))
    {
        ret = av_fifo_grow(packet_list, sizeof(MyAVPacketList));
        if (ret < 0)
        {
            goto fail;
        }
    }

    av_packet_move_ref(temp->pkt, pkt);

    av_fifo_generic_write(packet_list, temp, sizeof(MyAVPacketList), NULL); //写入到buffer中
    //更新队列中的数据
    nb_packets++;
    size += temp->pkt->size + sizeof(MyAVPacketList);
    duration += pkt->duration;
    SDL_CondSignal(cond); //唤醒条件变量
    if (ret < 0)
    {
    fail:
        delete temp;
    }
    return ret;
}

int PacketQueue::put(AVPacket *pkt)
{
    int ret;
    SDL_LockMutex(mutex);
    ret = put_internal(pkt);
    SDL_UnlockMutex(mutex);
    return ret;
}

void PacketQueue::start()
{
    SDL_LockMutex(mutex);
    abort_request = 0;
    SDL_UnlockMutex(mutex);
}

void PacketQueue::abort()
{
    SDL_LockMutex(mutex);
    abort_request = 1;
    SDL_CondSignal(cond);
    SDL_UnlockMutex(mutex);
}

void PacketQueue::destory()
{
    av_fifo_freep(&packet_list);
    SDL_DestroyCond(cond);
    SDL_DestroyMutex(mutex);
}

int PacketQueue::isAbort()
{
    return abort_request;
}

PacketQueue::~PacketQueue()
{
    std::cout << "destory packet queue" << std::endl;
    destory();
}