#include "FrameQueue.h"
extern "C"
{
#include "util.h"
}

FrameQueue::FrameQueue()
{
    // do nothing.
    logi("FrameQueue::FrameQueue()\n");
}

int FrameQueue::init(uint max_size, PacketQueue *pktq)
{
    this->max_size = max_size;
    frames = new Frame[max_size]();
    if (!frames)
    {
        logf("FrameQueue::init  Failed to alloc frame array.\n");
        return AVERROR(ENOMEM);
    }

    for (size_t i = 0; i < max_size; i++)
    {
        if (!frames[i].init())
        {
            return AVERROR(ENOMEM);
        }
    }

    mutex = SDL_CreateMutex();
    if (!mutex)
    {
        logf("FrameQueue::init  SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    cond = SDL_CreateCond();
    if (!cond)
    {
        logf("FrameQueue::init SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    rindex = 0;
    windex = 0;
    size = 0;
    rindex_shown = 0;

    this->pktq = pktq;

    return 0;
}

bool FrameQueue::is_empty(){
    return size == 0;
}

void FrameQueue::destory()
{
    delete[] frames;
    SDL_DestroyCond(cond);
    SDL_DestroyMutex(mutex);
}

Frame *FrameQueue::get()
{
    auto *frame = peekReadable();
    if (!frame)
    {
        return NULL;
    }
    SDL_LockMutex(mutex);
    if (++rindex >= max_size)
    {
        rindex = 0;
    }
    size--;
    SDL_CondSignal(cond);
    SDL_UnlockMutex(mutex);
    return frame;
}

Frame *FrameQueue::peek()
{
    return &frames[(rindex + rindex_shown) % max_size];
}

Frame *FrameQueue::peekLast()
{
    return &frames[rindex];
}

Frame *FrameQueue::peekReadable()
{
    //wait until we have a readable frame
    SDL_LockMutex(mutex);
    while (size - rindex_shown <= 0 && !pktq->isAbort())
    {
        SDL_CondWait(cond, mutex);
    }
    SDL_UnlockMutex(mutex);
    if (pktq->isAbort())
    {
        return NULL;
    }
    return peek();
}

Frame *FrameQueue::peekWritable()
{
    //wait until wa have a writable frame space
    SDL_LockMutex(mutex);
    while (!pktq->isAbort() && size >= max_size)
    {
        SDL_CondWait(cond, mutex);
    }
    SDL_UnlockMutex(mutex);
    if (pktq->isAbort())
    {
        return NULL;
    }
    return &frames[windex];
}

Frame *FrameQueue::put(Frame *frame)
{
    auto *wFrame = peekWritable();
    if (!wFrame)
    {
        return NULL;
    }
    wFrame->copy(frame);
    delete frame; //释放传进来的frame空间
    SDL_LockMutex(mutex);
    if (++windex >= max_size)
    {
        windex = 0;
    }
    size++;
    SDL_CondSignal(cond);
    SDL_UnlockMutex(mutex);
    return wFrame;
}

Frame *FrameQueue::put(AVFrame *frame, int duration, double pts, int64_t pos)
{
    auto *wFrame = peekWritable();
    if (!wFrame)
    {
        return NULL;
    }
    wFrame->width = frame->width;
    wFrame->height = frame->height;
    wFrame->format = frame->format;
    wFrame->sar = frame->sample_aspect_ratio;
    wFrame->duration = duration;
    wFrame->pts = pts;
    wFrame->pos = pos;

    av_frame_move_ref(wFrame->frame, frame);

    SDL_LockMutex(mutex);
    if (++windex >= max_size)
    {
        windex = 0;
    }
    size++;
    SDL_CondSignal(cond);
    SDL_UnlockMutex(mutex);
    return wFrame;
}

void FrameQueue::signal()
{
    SDL_LockMutex(mutex);
    SDL_CondSignal(cond);
    SDL_UnlockMutex(mutex);
}

FrameQueue::~FrameQueue()
{
    logi("FrameQueue::~FrameQueue()\n");
    destory();
}