#ifndef _FRAME_QUEUE_H
#define _FRAME_QUEUE_H

#define VIDEO_PICTURE_QUEUE_SIZE 3

extern "C"
{
#include <libavformat/avformat.h>
#include "PacketQueue.h"
}

class Frame
{
public:
    AVFrame *frame;
    double pts;      /* presentation timestamp for the frame */
    double duration; /* estimated duration of the frame */
    int64_t pos;     /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    int uploaded = 0;
    int flip_v;
    AVRational sar; /* 宽高比 */

    int init()
    {
        logi("Frame::init\n");
        frame = av_frame_alloc();
        return !!frame;
    }

    void copy(Frame *f)
    {
        av_frame_move_ref(frame, f->frame);
        pts = f->pts;
        duration = f->duration;
        pos = f->pos;
        width = f->width;
        height = f->height;
        format = f->format;
        sar = f->sar;
        uploaded = f->uploaded;
    }

    ~Frame()
    {
        logi("~Frame()\n");
        av_frame_free(&frame);
    }
};

class FrameQueue
{
private:
    Frame *frames;    //数组
    int rindex;       //正在读取的index
    int windex;       //正在写入的位置index
    int size;         //frames中可读取的大小
    int max_size;     //frames可写入的size
    int rindex_shown; //rindex是否已经显示过了，0 or 1
    SDL_mutex *mutex;
    SDL_cond *cond;
    PacketQueue *pktq;

public:
    FrameQueue();
    int init(uint max_size, PacketQueue *);
    bool is_empty();
    void destory();
    /**
     * 返回下一个没有显示过的frame, 不改变index的位置
     **/
    Frame *peek();
    /**
     * 返回下一个frame，不管显示或者没有显示过，不改变index的位置
     * */
    Frame *peekLast();
    /**
     * 返回可写入的frame地址空间
     * */
    Frame *peekWritable();
    /**
     * 返回可读取的frame地址，不改变index的位置
     * */
    Frame *peekReadable();
    /**
     * 读取一个没有显示过的frame，改变rindex的位置
     * 列表为空，则等待
     * */
    Frame *get();
    /**
     * 将frame写入到可写入的位置，已写满则等待
     * */
    Frame *put(Frame *);

    Frame *put(AVFrame *frame, int duration, double pts, int64_t pos);

    /**
     * 唤醒cond
     * */
    void signal();
    ~FrameQueue();
};

#endif