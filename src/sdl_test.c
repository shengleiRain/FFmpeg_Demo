#include "sdl_test.h"
#include <SDL2/SDL.h>

#define SCREEN_W 500
#define SCREEN_H 500
#define REFRESH_VIDEO_EVENT (SDL_USEREVENT + 1)
#define BREAK_EVENT (SDL_USEREVENT + 2)
#define BLOCK_SIZE 4096000

void hello_sdl2()
{
    SDL_Event event;
    int quit = 1;
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window *window = SDL_CreateWindow("SDL window", 0, 0, 640, 480, SDL_WINDOW_SHOWN);
    if (!window)
    {
        SDL_Log("Failed to create window.\n");
        goto __EXIT;
    }

    SDL_Renderer *render = SDL_CreateRenderer(window, -1, 0);
    if (!render)
    {
        SDL_Log("Failed to create renderer.\n");
        goto __WINDOW;
    }

    SDL_SetRenderDrawColor(render, 255, 0, 0, 255);
    SDL_RenderClear(render);
    SDL_RenderPresent(render);

    do
    {
        SDL_WaitEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            quit = 0;
            break;
        default:
            SDL_Log("event type:%d", event.type);
            break;
        }
    } while (quit);

    SDL_DestroyRenderer(render);
__WINDOW:
    SDL_DestroyWindow(window);

__EXIT:
    SDL_Quit();
}

int refresh_video(int *thread_exit)
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

void play_yuv(const char *src_file, unsigned int pixel_w, unsigned int pixel_h)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    int quit = 0;
    SDL_Event event;
    SDL_Init(SDL_INIT_VIDEO);
    int screen_w = pixel_w;
    int screen_h = pixel_h;

    int buffer_size = pixel_h * pixel_w * 12 / 8;
    unsigned char buffer[buffer_size];

    window = SDL_CreateWindow(src_file, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pixel_w, pixel_h);
    if (!texture)
    {
        SDL_Log("Failed to create texture. error: %s\n", SDL_GetError());
        goto __RENDERER;
    }

    SDL_Rect rect;
    SDL_Thread *thread = SDL_CreateThread(refresh_video, NULL, &quit);

    FILE *fp = fopen(src_file, "rb+");
    if (!fp)
    {
        SDL_Log("Could not open this file: %s\n", src_file);
        goto __RENDERER;
    }

    while (1)
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
            if (fread(buffer, 1, buffer_size, fp) != buffer_size)
            {
                //Loop
                fseek(fp, 0, SEEK_SET);
                fread(buffer, 1, buffer_size, fp);
            }
            SDL_UpdateTexture(texture, NULL, buffer, pixel_w);

            rect.x = 0;
            rect.y = 0;
            rect.w = screen_w;
            rect.h = screen_h;

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, &rect);
            SDL_RenderPresent(renderer);
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
    }
__RENDERER:
    SDL_DestroyRenderer(renderer);
__WINDOW:
    SDL_DestroyWindow(window);
__EXIT:
    SDL_Quit();
}

static Uint8 *buffer = NULL;
static Uint8 *audio_pos = NULL;
static size_t buffer_len = 0;

void audio_read_data(void *userdata, Uint8 *stream, int len)
{

    if (buffer_len == 0)
    {
        return;
    }
    //将stream中的数据清空了
    SDL_memset(stream, 0, len);
    len = (len < buffer_len) ? len : buffer_len;
    printf("len=%d\n", len);
    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);

    audio_pos += len;
    buffer_len -= len;
}

void play_pcm(const char *src_file)
{
    int ret = 0;
    SDL_Init(SDL_INIT_AUDIO);

    //打开pcm音频文件
    FILE *fp = fopen(src_file, "rb");
    if (!fp)
    {
        SDL_Log("Failed to open file.\n");
        goto end;
    }
    //分配缓存空间
    buffer = (uint8_t *)malloc(BLOCK_SIZE);
    if (!buffer)
    {
        SDL_Log("Faield to alloc memory.\n");
        goto end;
    }

    SDL_AudioSpec spec;
    spec.channels = 2;
    spec.freq = 44100;
    spec.userdata = NULL;
    spec.samples = 1024;
    spec.format = AUDIO_S16SYS;
    spec.callback = audio_read_data;
    if (SDL_OpenAudio(&spec, NULL))
    {
        SDL_Log("Failed to open audio device.\n");
        goto end;
    }
    //play audio
    SDL_PauseAudio(0);

    do
    {
        //从pcm文件中读取数据到buffer中
        buffer_len = fread(buffer, 1, BLOCK_SIZE, fp);
        audio_pos = buffer;

        //播放过程中，需要稍微延时一下下
        while (audio_pos < (buffer + buffer_len))
        {
            SDL_Delay(1);
        }
    } while (buffer_len != 0);

    SDL_CloseAudio();

end:
    if (fp)
    {
        fclose(fp);
    }
    if (buffer)
    {
        free(buffer);
    }
    SDL_Quit();
}