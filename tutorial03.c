#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>

#include <stdio.h>

//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

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
//        SDL_Delay(40);
    }
    thread_exit = 0;
    thread_pause = 0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

int quit = 0;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

  AVPacketList *pkt1;
  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;
  
  
  SDL_LockMutex(q->mutex);
  
  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);
  
  SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;
  
  SDL_LockMutex(q->mutex);
  
  for(;;) {
    
    if(quit) {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
	q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {

  static AVPacket pkt;
  static uint8_t *audio_pkt_data = NULL;
  static int audio_pkt_size = 0;
  static AVFrame frame;

  int len1, data_size = 0;

  for(;;) {
    while(audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
      if(len1 < 0) {
	/* if error, skip frame */
	audio_pkt_size = 0;
	break;
      }
      audio_pkt_data += len1;
      audio_pkt_size -= len1;
      if (got_frame)
      {
          data_size = 
            av_samples_get_buffer_size
            (
                NULL, 
                aCodecCtx->channels,
                frame.nb_samples,
                aCodecCtx->sample_fmt,
                1
            );
          memcpy(audio_buf, frame.data[0], data_size);
      }
      if(data_size <= 0) {
	/* No data yet, get more frames */
	continue;
      }
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if(pkt.data)
      av_free_packet(&pkt);

    if(quit) {
      return -1;
    }

    if(packet_queue_get(&audioq, &pkt, 1) < 0) {
      return -1;
    }
    audio_pkt_data = pkt.data;
    audio_pkt_size = pkt.size;
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
  int len1, audio_size;

  static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  static unsigned int audio_buf_size = 0;
  static unsigned int audio_buf_index = 0;

  while(len > 0) {
    if(audio_buf_index >= audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(aCodecCtx, audio_buf, audio_buf_size);
      if(audio_size < 0) {
	/* If error, output silence */
	audio_buf_size = 1024; // arbitrary?
	memset(audio_buf, 0, audio_buf_size);
      } else {
	audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    len1 = audio_buf_size - audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
    len -= len1;
    stream += len1;
    audio_buf_index += len1;
  }
}


int main(int argc, char* argv[]) {
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    int videoStreamIndex = -1, audioStreamIndex = -1;
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

    AVCodecContext  *aCodecCtx = NULL;
    AVCodec         *aCodec = NULL;

    //------------SDL----------------
    int screen_w, screen_h;
    SDL_Window *screen;
    SDL_Renderer *sdlRenderer;
    SDL_Texture *sdlTexture;
    SDL_Rect sdlRect;
    SDL_Thread *video_tid;
    SDL_Event event;
    SDL_AudioSpec   wanted_spec, spec;

    AVDictionary        *videoOptionsDict   = NULL;
    AVDictionary        *audioOptionsDict   = NULL;

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
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                videoStreamIndex < 0) {
            videoStreamIndex = i;
            pAVCodecCtxOrigin = pFormatCtx->streams[i]->codec;
//            break;
        }
        if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                audioStreamIndex < 0) {
            audioStreamIndex = i;
            aCodecCtx = pFormatCtx->streams[i]->codec;
        }
    }

    if (-1 == videoStreamIndex) {
        fprintf(stderr, "error: cannot find video stream\n");
        return -1;
    }

    if(-1 == audioStreamIndex) {
        fprintf(stderr, "error: cannot find audio stream\n");
        return -1;
    }

    // Set audio settings from codec info
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;

    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        return -1;
    }
    aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
    if(!aCodec) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }
    avcodec_open2(aCodecCtx, aCodec, &audioOptionsDict);

    // audio_st = pFormatCtx->streams[index]
    packet_queue_init(&audioq);
    SDL_PauseAudio(0);


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

    for (;;) {
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {
//            while (1) {
//                if (av_read_frame(pFormatCtx, packet) < 0)
//                    thread_exit = 1;
//
//                if (packet->stream_index == videoStreamIndex)
//                    break;
//            }

            if (av_read_frame(pFormatCtx, packet) < 0) {
                thread_exit = 1;
                break;
            }

            if (packet->stream_index == videoStreamIndex) {
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
            } else if (packet->stream_index == audioStreamIndex) {
                packet_queue_put(&audioq, packet);
            } else {
                av_packet_unref(packet);
            }

        } else if (event.type == SDL_KEYDOWN) {
            //Pause
            if (event.key.keysym.sym == SDLK_SPACE) {
                thread_pause = !thread_pause;
            } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                thread_exit = 1;
                quit = 1;
            }
        } else if (event.type == SDL_QUIT) {
            thread_exit = 1;
            quit = 1;
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

