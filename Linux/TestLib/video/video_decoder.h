/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2023-07-04 13:33:03
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:27:41
 * @FilePath: /data-engine/modules/video/video_decoder.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef VIDEO_VIDEO_DECODER_H_
#define VIDEO_VIDEO_DECODER_H_

#include <vector>

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


class VideoDecoder final {
public:
  ~VideoDecoder();
  int init(int width, int height, AVCodecID codec_id);
  int decode(std::vector<char> &&input, std::vector<char> &output,
             AVPixelFormat &output_format);

private:
  int libav_decode(AVCodecContext *avctx, AVFrame *frame, AVFrame *&hwframe,
                   int *got_frame, AVPacket *pkt);

  AVCodec *codec_;
  AVCodecContext *codec_context_;
  AVFrame *picture_;

  AVPacket packet_;
  std::vector<char> decode_buffer_;
  std::vector<uint8_t> packet_buffer_;

  AVHWDeviceType hw_type_ = AV_HWDEVICE_TYPE_CUDA;
  AVPixelFormat hwfmt_ = AV_PIX_FMT_NONE;

  int width_;
  int height_;
};

#endif /* VIDEO_VIDEO_DECODER_H_ */
