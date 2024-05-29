/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2023-07-04 13:33:03
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:26:45
 * @FilePath: /data-engine/modules/video/mp4toavi.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef MP4TO_AVI_H

#define MP4TO_AVI_H

#include <string>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/buffer.h"
#include "libavutil/error.h"
#include "libavutil/hwcontext.h"
#include "libavutil/mem.h"
}
#include <thread>
#include "ffmpeg_decoder.h"
#include "avilib.h"
#include <future>
#include <mutex>

class Mp4toAvi {
public:
  Mp4toAvi(/* args */);
  ~Mp4toAvi();

  bool init(std::string strpath);
  void run();

  std::string video_file_name_;
  AVFormatContext *format_ctx_{nullptr};
  int stream_index_ = 0; // video stream num
  AVPacket pkt_;
  avi_t *m_pavi;
  std::mutex m_lock;
  FFmpegDecoder m_ffmpeg;

  int width_ = 0;
  int height_ = 0;
};

#endif /* VIDEO_VIDEO_UTILS_H_ */
