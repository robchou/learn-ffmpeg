#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>

#define __STDC_CONSTANT_MACROS

#include <stdio.h>

//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void *opaque) {
    thread_exit = 0;
    thread_pause = 0;

    while (!thread_exit) {
        if (!thread_pause) {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(40);
    }
    thread_exit = 0;
    thread_pause = 0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

int main(int argc, char* argv[]) {
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    int videoStreamIndex = -1;
    AVCodecContext *pAVCodecCtxOrigin = NULL;
    AVCodecContext *pAVCodecCtx = NULL;
    AVCodec *pAVCodec = NULL;
    AVFrame *pFrame = NULL;
    AVFrame *pFrameYUV = NULL;
    uint8_t *buffer = NULL;
    int numBytes;
    struct SwsContext *img_convert_ctx = NULL;
    AVPacket *packet = av_malloc(sizeof(AVPacket));
    int got_picture;

    //------------SDL----------------
    int screen_w, screen_h;
    SDL_Window *screen;
    SDL_Renderer *sdlRenderer;
    SDL_Texture *sdlTexture;
    SDL_Rect sdlRect;
    SDL_Thread *video_tid;
    SDL_Event event;

    // register all encoders/decoders, muxers/demuxers and protocols
    av_register_all();
//    avformat_network_init();

    // open video file
    if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) !=0 ) {
        printf("error: cannot open input stream\n");
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        fprintf(stderr, "error: cannot find stream info\n");
        return -1;
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    for (int i = 0; i < pFormatCtx->nb_streams; ++i) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            pAVCodecCtxOrigin = pFormatCtx->streams[i]->codec;
            break;
        }
    }

    if (-1 == videoStreamIndex) {
        fprintf(stderr, "error: cannot find video stream\n");
        return -1;
    }

    // Find the decoder for the video stream
    pAVCodec = avcodec_find_decoder(pAVCodecCtxOrigin->codec_id);

    if (NULL == pAVCodec) {
        fprintf(stderr, "Unsupported codec\n");
        return -1;
    }

    pAVCodecCtx = avcodec_alloc_context3(pAVCodec);

    if (avcodec_copy_context(pAVCodecCtx, pAVCodecCtxOrigin) != 0) {
        fprintf(stderr, "cannot copy decode context\n");
        return -1;
    }

    if (avcodec_open2(pAVCodecCtx, pAVCodec, NULL) < 0) {
        fprintf(stderr, "cannot open codec\n");
        return -1;
    }

    // Allocate video frame
    pFrame = av_frame_alloc();

    // Allocate RGB video frame
    pFrameYUV = av_frame_alloc();

    if (pFrame == NULL || pFrameYUV == NULL) {
        fprintf(stderr, "error: cannot allocate video frame\n");
        return -1;
    }

    // Determin required buffer size and allocate buffer
//    numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pAVCodecCtx->width, pAVCodecCtx->height);
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pAVCodecCtx->width, pAVCodecCtx->height, 1);
    buffer = av_malloc(numBytes * sizeof(uint8_t));
    // Assign appropriate parts of buffer to image planes in pFrameYUV
    // Note that pFrameYUV is an AVFrame, but AVFrame is a superset of AVPicture
//    avpicture_fill((AVPicture*)pFrameYUV, buffer, AV_PIX_FMT_RGB24, pAVCodecCtx->width, pAVCodecCtx->height);
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer,
                         AV_PIX_FMT_YUV420P, pAVCodecCtx->width, pAVCodecCtx->height, 1);


    img_convert_ctx = sws_getContext(
            pAVCodecCtx->width,
            pAVCodecCtx->height,
            pAVCodecCtx->pix_fmt,
            pAVCodecCtx->width,
            pAVCodecCtx->height,
//            AV_PIX_FMT_RGB24,
            AV_PIX_FMT_YUV420P,
            SWS_BILINEAR,
            NULL,
            NULL,
            NULL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    //SDL 2.0 Support for multiple windows
    screen_w = pAVCodecCtx->width;
    screen_h = pAVCodecCtx->height;
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h, SDL_WINDOW_OPENGL);

    if (!screen) {
        printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
        return -1;
    }
    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pAVCodecCtx->width,
                                   pAVCodecCtx->height);

    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = screen_w;
    sdlRect.h = screen_h;
    video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

    for (; ;) {
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {
            while (1) {
                if (av_read_frame(pFormatCtx, packet) < 0)
                    thread_exit = 1;

                if (packet->stream_index == videoStreamIndex)
                    break;
            }

            int ret = avcodec_decode_video2(pAVCodecCtx, pFrame, &got_picture, packet);

            if (ret < 0) {
                printf("error: decode failed!\n");
                return -1;
            }

            if (got_picture) {
                //Convert the image frome its native format to RGB
                sws_scale(img_convert_ctx,
                          (uint8_t* const * const *)pFrame->data,
                          pFrame->linesize,
                          0,
                          pAVCodecCtx->height,
                          pFrameYUV->data,
                          pFrameYUV->linesize);

                //SDL---------------------------
                SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                SDL_RenderClear(sdlRenderer);
                //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
                SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
                SDL_RenderPresent(sdlRenderer);
                //SDL End-----------------------
            }
            av_packet_unref(packet);

        } else if (event.type == SDL_KEYDOWN) {
            //Pause
            if (event.key.keysym.sym == SDLK_SPACE)
                thread_pause = !thread_pause;
        } else if (event.type == SDL_QUIT) {
            thread_exit = 1;
        } else if (event.type == SFM_BREAK_EVENT) {
            break;
        }
    }

    sws_freeContext(img_convert_ctx);
    SDL_Quit();
    av_free(buffer);
    av_free(pFrame);
    av_free(pFrameYUV);

    // Close the codecs
    avcodec_close(pAVCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    return 0;
}

