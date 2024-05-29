#include <stdio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

void saveFrame(AVFrame *avFrame, int width, int height, int frameIndex);

int main(int argc, char *argv[]) {
  AVFormatContext *pFormatCtx = NULL;  // [1]

  FILE *fp_save_h264_ = fopen(
      "/home/lixiang/Documents/hsp/WorkNotes/Some_Libs/FFMPEG_Test/second/"
      "640x360.h264",
      "wb+");

 // char *fileName =
 //     "/home/lixiang/Documents/hsp/WorkNotes/Some_Libs/FFMPEG_Test/"
 //     "640x360.mp4";
 
 
 char* fileName = "/home/lixiang/Documents/test_record/2024-04-15_09-54-26/2024-04-15_09-54-37/Video_12_r_ca_2024-04-15_09-54-38.mp4";

  int ret = avformat_open_input(&pFormatCtx, fileName, NULL, NULL);  // [2]
  if (ret < 0) {
    // couldn't open file
    printf("Could not open file %s\n", fileName);

    return -1;
  }

  ret = avformat_find_stream_info(pFormatCtx, NULL);  //[3]
  if (ret < 0) {
    // couldn't find stream information
    printf("Could not find stream information %s\n", fileName);

    // exit with error
    return -1;
  }

  av_dump_format(pFormatCtx, 0, fileName, 0);  // [4]

  int i;

  // The stream's information about the codec is in what we call the
  // "codec context." This contains all the information about the codec that
  // the stream is using
  AVCodecContext *pCodecCtxOrig = NULL;
  AVCodecContext *pCodecCtx = NULL;

  // Find the first video stream
  int videoStream = -1;
  for (i = 0; i < pFormatCtx->nb_streams; i++) {
    // check the General type of the encoded data to match
    // AVMEDIA_TYPE_VIDEO
    if (pFormatCtx->streams[i]->codecpar->codec_type ==
        AVMEDIA_TYPE_VIDEO)  // [5]
    {
      videoStream = i;
      break;
    }
  }

  if (videoStream == -1) {
    // didn't find a video stream
    return -1;
  }

  // But we still have to find the actual codec and open it:
  const AVCodec *pCodec = NULL;

  pCodec = avcodec_find_decoder(
      pFormatCtx->streams[videoStream]->codecpar->codec_id);  // [6]
  if (pCodec == NULL) {
    // codec not found
    printf("Unsupported codec!\n");

    // exit with error
    return -1;
  }

  pCodecCtxOrig = avcodec_alloc_context3(pCodec);  // [7]
  ret = avcodec_parameters_to_context(
      pCodecCtxOrig, pFormatCtx->streams[videoStream]->codecpar);

  // Copy context
  // avcodec_copy_context deprecation
  // http://ffmpeg.org/pipermail/libav-user/2017-September/010615.html
  // ret = avcodec_copy_context(pCodecCtx, pCodecCtxOrig);
  pCodecCtx = avcodec_alloc_context3(pCodec);  // [7]
  ret = avcodec_parameters_to_context(
      pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
  if (ret != 0) {
    // error copying codec context
    printf("Could not copy codec context.\n");

    // exit with error
    return -1;
  }

  // Open codec
  ret = avcodec_open2(pCodecCtx, pCodec, NULL);  // [8]
  if (ret < 0) {
    // Could not open codec
    printf("Could not open codec.\n");

    // exit with error
    return -1;
  }

  // Now we need a place to actually store the frame:
  AVFrame *pFrame = NULL;

  // Allocate video frame
  pFrame = av_frame_alloc();  // [9]
  if (pFrame == NULL) {
    // Could not allocate frame
    printf("Could not allocate frame.\n");

    // exit with error
    return -1;
  }

  /**
   * Since we're planning to output PPM files, which are stored in 24-bit
   * RGB, we're going to have to convert our frame from its native format
   * to RGB. ffmpeg will do these conversions for us. For most projects
   * (including ours) we're going to want to convert our initial frame to
   * a specific format. Let's allocate a frame for the converted frame
   * now.
   */

  // Allocate an AVFrame structure
  AVFrame *pFrameRGB = NULL;
  pFrameRGB = av_frame_alloc();
  if (pFrameRGB == NULL) {
    // Could not allocate frame
    printf("Could not allocate frame.\n");

    // exit with error
    return -1;
  }

  // Even though we've allocated the frame, we still need a place to put
  // the raw data when we convert it. We use avpicture_get_size to get
  // the size we need, and allocate the space manually:
  uint8_t *buffer = NULL;
  int numBytes;

  // Determine required buffer size and allocate buffer
  // numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width,
  // pCodecCtx->height);
  // https://ffmpeg.org/pipermail/ffmpeg-devel/2016-January/187299.html
  // what is 'linesize alignment' meaning?:
  // https://stackoverflow.com/questions/35678041/what-is-linesize-alignment-meaning
  numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width,
                                      pCodecCtx->height, 32);  // [10]
  buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));   // [11]

  /**
   * Now we use avpicture_fill() to associate the frame with our newly
   * allocated buffer. About the AVPicture cast: the AVPicture struct is
   * a subset of the AVFrame struct - the beginning of the AVFrame struct
   * is identical to the AVPicture struct.
   */
  // Assign appropriate parts of buffer to image planes in pFrameRGB
  // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
  // of AVPicture
  // Picture data structure - Deprecated: use AVFrame or imgutils functions
  // instead
  // https://www.ffmpeg.org/doxygen/3.0/structAVPicture.html#a40dfe654d0f619d05681aed6f99af21b
  // avpicture_fill( // [12]
  //     (AVPicture *)pFrameRGB,
  //     buffer,
  //     AV_PIX_FMT_RGB24,
  //     pCodecCtx->width,
  //     pCodecCtx->height
  // );
  av_image_fill_arrays(  // [12]
      pFrameRGB->data, pFrameRGB->linesize, buffer, AV_PIX_FMT_RGB24,
      pCodecCtx->width, pCodecCtx->height, 32);

  // Finally! Now we're ready to read from the stream!

  /**
   * What we're going to do is read through the entire video stream by
   * reading in the packet, decoding it into our frame, and once our
   * frame is complete, we will convert and save it.
   */

  struct SwsContext *sws_ctx = NULL;

  AVPacket *pPacket = av_packet_alloc();
  if (pPacket == NULL) {
    // couldn't allocate packet
    printf("Could not alloc packet,\n");

    // exit with error
    return -1;
  }

  // initialize SWS context for software scaling
  sws_ctx = sws_getContext(  // [13]
      pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width,
      pCodecCtx->height,
      AV_PIX_FMT_RGB24,  // sws_scale destination color scheme
      SWS_BILINEAR, NULL, NULL, NULL);

  // The numer in the argv[2] array is in a string representation. We
  // need to convert it to an integer.
  int maxFramesToDecode = 2000;

  /**
   * The process, again, is simple: av_read_frame() reads in a packet and
   * stores it in the AVPacket struct. Note that we've only allocated the
   * packet structure - ffmpeg allocates the internal data for us, which
   * is pointed to by packet.data. This is freed by the av_free_packet()
   * later. avcodec_decode_video() converts the packet to a frame for us.
   * However, we might not have all the information we need for a frame
   * after decoding a packet, so avcodec_decode_video() sets
   * frameFinished for us when we have decoded enough packets the next
   * frame.
   * Finally, we use sws_scale() to convert from the native format
   * (pCodecCtx->pix_fmt) to RGB. Remember that you can cast an AVFrame
   * pointer to an AVPicture pointer. Finally, we pass the frame and
   * height and width information to our SaveFrame function.
   */

  i = 0;
  
  static int packetCount = 0;
  while (av_read_frame(pFormatCtx, pPacket) >= 0)  // [14]
  {
    // Is this a packet from the video stream?
    if (pPacket->stream_index == videoStream) {
    
    printf("aaaaaaaaaaaaaaaaaa packetCount: %d \n",++packetCount);
    
    printf("ddddddddddddddddd packet size: %d \n", pPacket->size);
      // Decode video frame
      ret = avcodec_send_packet(pCodecCtx, pPacket);  // [15]
      
       printf("bbbbbbbbbbbbbbbbbbbbbbbb \n");

      fwrite(pPacket->data, pPacket->size, 1, fp_save_h264_);

      if (ret < 0) {
        // could not send packet for decoding
        printf("Error sending packet for decoding.\n");

        // exit with eror
        return -1;
      }

      while (ret >= 0) {
        ret = avcodec_receive_frame(pCodecCtx, pFrame);  // [15]
        
        printf("cccccccccccccccccccccccccc \n");

        if (ret == AVERROR(EAGAIN) ) {
          printf("ret == AVERROR(EAGAIN)\n");
          break;
        } else if (ret == AVERROR_EOF) {
          printf("ret == AVERROR_EOF\n");
          break;
        } else if (ret < 0) {
          // could not decode packet
          printf("Error while decoding.\n");
          return -1;
        }

        // Convert the image from its native format to RGB
        sws_scale(  // [16]
            sws_ctx, (uint8_t const *const *)pFrame->data, pFrame->linesize, 0,
            pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

        // Save the frame to disk
        if (++i <= maxFramesToDecode) {
          // save the read AVFrame into ppm file
          // saveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
          // print log information
          printf(
              "Frame %c (%d) pts %d dts %d key_frame %d "
              "[coded_picture_number %d, display_picture_number %d,"
              " %dx%d]\n",
              av_get_picture_type_char(pFrame->pict_type),
              pCodecCtx->frame_number, pFrameRGB->pts, pFrameRGB->pkt_dts,
              pFrameRGB->key_frame, pFrameRGB->coded_picture_number,
              pFrameRGB->display_picture_number, pCodecCtx->width,
              pCodecCtx->height);
        } else {
          break;
        }
      }

      if (i > maxFramesToDecode) {
        // exit loop and terminate
        break;
      }
    }

    // Free the packet that was allocated by av_read_frame
    // [FFmpeg-cvslog] avpacket: Replace av_free_packet with
    // av_packet_unref
    // https://lists.ffmpeg.org/pipermail/ffmpeg-cvslog/2015-October/094920.html
    av_packet_unref(pPacket);
  }

  fclose(fp_save_h264_);

  // Free the RGB image
  av_free(buffer);
  av_frame_free(&pFrameRGB);
  av_free(pFrameRGB);

  // Free the YUV frame
  av_frame_free(&pFrame);
  av_free(pFrame);

  // Close the codecs
  avcodec_close(pCodecCtx);
  avcodec_close(pCodecCtxOrig);

  // Close the video file
  avformat_close_input(&pFormatCtx);

  return 0;
}

void saveFrame(AVFrame *avFrame, int width, int height, int frameIndex) {
  FILE *pFile;
  char szFilename[32];
  int y;

  // Open file
  sprintf(szFilename, "frame%d.ppm", frameIndex);
  pFile = fopen(szFilename, "wb");
  if (pFile == NULL) {
    return;
  }

  // Write header
  fprintf(pFile, "P6\n%d %d\n255\n", width, height);

  // Write pixel data
  for (y = 0; y < height; y++) {
    fwrite(avFrame->data[0] + y * avFrame->linesize[0], 1, width * 3, pFile);
  }

  // Close file
  fclose(pFile);
}
