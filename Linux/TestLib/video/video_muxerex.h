/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2024-04-18 15:05:53
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:30:19
 * @FilePath: /data-engine/modules/video/video_muxerex.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef VIDEO_VIDEO_MUXEREX_H_
#define VIDEO_VIDEO_MUXEREX_H_
#include "avilib.h"
#include "ffmpeg_decoder.h"
#include "video_muxer.h"
#include <mutex>

class VideoMuxerEx : public VideoMuxer {
public:
  ~VideoMuxerEx();

  virtual bool init(std::string filename, int width, int height);
  virtual void writeFrame(unsigned char *buffer, int size);
  void RGB_TO_JPEG(unsigned char *rgb, int image_width, int image_height,
                   int quality /*= 90*/, unsigned char *poutdata,
                   size_t &outsize);

private:
  FFmpegDecoder m_ffmpeg;
  std::mutex m_lock;
  avi_t *m_pavi;
  unsigned char *m_ptempdata;
  bool m_bneedavi = true;
};

#endif