/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2023-10-08 10:07:03
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:30:40
 * @FilePath: /data-engine/modules/video/video_transcoder.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef IMAGE_TRANSCODER_H__
#define IMAGE_TRANSCODER_H__

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#ifdef __cplusplus
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
}
#endif

class ImageData;

// video encoder class
// video encoder
// encode camera picture to mp4 file
class VideoTranscoder {
public:
  VideoTranscoder();
  ~VideoTranscoder();

  bool startTranscoder(); // try to open the video file to write the frames
  void stopTranscoder();  // stop the encode process
  int transcodeSingleFrame(unsigned char *p_image_data, int frame_len,
                           ImageData &output);

  void setEncodeImageWidth(int width);
  int getEncodeImageWidth();

  void setEncodeImageHeight(int height);
  int getEncodeImageHeight();

  void setEncoderType(int encoder_type);
  int getEncoderType();

private:
  int setHwframeCtx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx);
  int encodeWrite(AVFrame *frame, ImageData &output);
  int convertYuvj422pToYuv420p(AVFrame *src_frame, AVFrame *dst_frame);
  int64_t getSampleRate(void);
  int64_t getCurrentSysTime(void);
  long getSysTimeInSec(void);
  long getTimeelapse(void);

  bool initEncoderCodec();
  bool initDecoderCodec();
  void setEncoderCoversion();
  int decEnc(AVPacket *pkt, ImageData &output);

  AVCodecContext *encoder_codec_ctx_ = nullptr;
  AVCodecContext *decoder_codec_ctx_ = nullptr;
  AVCodec *encoder_codec_ = nullptr;
  AVCodec *decoder_codec_ = nullptr;
  AVPacket *packet = nullptr;
  bool trans_ = false;
  AVHWDeviceType hwtype_ = AV_HWDEVICE_TYPE_NONE; // hardware type
  AVCodecParserContext *codec_parser_ = nullptr;
  AVPixelFormat hwfmt_ = AV_PIX_FMT_NONE;
  AVBufferRef *hw_device_ctx_ = nullptr;
  int width_ = 1280;
  int height_ = 720;
  SwsContext *conversion_ = nullptr; // conversion from bgr24 to yuv420p
  int fps_ = 30;
  int64_t frame_num_ = 0; // frame statistics
  long first_encode_timestamp_ =
      0; // record last time recv time stamp(unit : s)
};

#endif
