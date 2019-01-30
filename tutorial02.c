#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame){
    FILE *pFile;
    char szFilename[32];
    int y;

    //Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile = fopen(szFilename, "wb");
    if(pFile == NULL) {
        fprintf("error: failed to open %s\n", szFilename);
        return;
    }

    //Wirte header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    //Write piexl data
    for(y = 0; y < height; y++)
        fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, width*3, pFile);

    //Close file
    fclose(pFile);
}

int main(int argc, char* argv[]) {
    // register all encoders/decoders, muxers/demuxers and protocols
    av_register_all();

    AVFormatContext *pFormatCtx = NULL;
    int videoStreamIndex = -1;
    AVCodecContext *pAVCodecCtxOrigin = NULL;
    AVCodecContext *pAVCodecCtx = NULL;
    AVCodec *pAVCodec = NULL;
    AVFrame *pFrame = NULL;
    AVFrame *pFrameRGB = NULL;
    uint8_t *buffer = NULL;
    int numBytes;
    struct SwsContext *pSwsCtx = NULL;
    AVPacket packet;
    int frameFinished;

    // open video file
    avformat_open_input(&pFormatCtx, argv[1], NULL, NULL);

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
    pFrameRGB = av_frame_alloc();

    if (pFrame == NULL || pFrameRGB == NULL) {
        fprintf(stderr, "error: cannot allocate video frame\n");
        return -1;
    }

    // Determin required buffer size and allocate buffer
    numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pAVCodecCtx->width, pAVCodecCtx->height);
    buffer = av_malloc(numBytes * sizeof(uint8_t));

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset of AVPicture
    avpicture_fill((AVPicture*)pFrameRGB, buffer, AV_PIX_FMT_RGB24, pAVCodecCtx->width, pAVCodecCtx->height);

    pSwsCtx = sws_getContext(
            pAVCodecCtx->width,
            pAVCodecCtx->height,
            pAVCodecCtx->pix_fmt,
            pAVCodecCtx->width,
            pAVCodecCtx->height,
            AV_PIX_FMT_RGB24,
            SWS_BILINEAR,
            NULL,
            NULL,
            NULL);

    int i = 0;

    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStreamIndex) {
            // Decode video frame
            avcodec_decode_video2(pAVCodecCtx, pFrame, &frameFinished, &packet);

            if (frameFinished) {
                //Convert the image frome its native format to RGB
                sws_scale(pSwsCtx,
                          (uint8_t* const * const *)pFrame->data,
                          pFrame->linesize,
                          0,
                          pAVCodecCtx->height,
                          pFrameRGB->data,
                          pFrameRGB->linesize);

                //Save the frame to disk
                if (++i <= 50) {
                    SaveFrame(pFrameRGB, pAVCodecCtx->width, pAVCodecCtx->height, i);
                }

            }
        }

        av_free_packet(&packet);
    }

    av_free(buffer);
    av_free(pFrame);
    av_free(pFrameRGB);

    // Close the codecs
    avcodec_close(pAVCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    return 0;
}

