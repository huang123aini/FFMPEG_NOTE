/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2023-09-06 08:28:02
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:29:20
 * @FilePath: /data-engine/modules/video/video_encoder.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef VIDEO_VIDEO_ENCODER_H_
#define VIDEO_VIDEO_ENCODER_H_

#include <vector>

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


class VideoEncoder final {
public:
  bool init(int width, int height);
  bool encode(std::vector<char> &&input, std::vector<char> &output,
              AVPixelFormat format);

private:
  int setHwFrameCtx();
  AVCodecContext *codec_context_ = nullptr;
  AVCodec *codec_ = nullptr;
  AVBufferRef *hw_device_ctx_ = nullptr;

  int width_ = 0;
  int height_ = 0;
};


#endif /* VIDEO_VIDEO_ENCODER_H_ */
