/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2024-04-22 17:36:16
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:29:47
 * @FilePath: /data-engine/modules/video/video_muxer.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef VIDEO_VIDEO_MUXER_H_
#define VIDEO_VIDEO_MUXER_H_

#include <memory>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
}

class VideoMuxer {
public:
  ~VideoMuxer();

  virtual bool init(std::string filename, int width, int height);
  virtual void writeFrame(unsigned char *buffer, int size);
  void setCodecId(AVCodecID codec_id);
  AVCodecID getCodecId() const { return codec_id_; }
  auto getPts() const { return pts_; }
  auto width() const { return width_; }
  auto height() const { return height_; }
  void close();
  std::string fileName() const { return filename_; }

protected:
  bool addStream();
  bool openVideo();

  AVFormatContext *output_format_context_ = nullptr;
  AVStream *out_stream_ = nullptr;
  AVCodecContext *codec_context_ = nullptr;
  AVCodec *codec_ = nullptr;
  AVCodecID codec_id_ = AV_CODEC_ID_H264;
  std::int64_t pts_ = 0;

  bool is_initialized_suc_ = false;
  int width_ = 0;
  int height_ = 0;
  std::string filename_;
};

#endif /* VIDEO_VIDEO_MUXER_H_ */
