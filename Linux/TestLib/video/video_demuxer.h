/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2024-04-22 17:57:46
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:28:40
 * @FilePath: /data-engine/modules/video/video_demuxer.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef IMAGEDEMUXER_H
#define IMAGEDEMUXER_H
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/buffer.h"
#include "libavutil/error.h"
#include "libavutil/hwcontext.h"
#include "libavutil/mem.h"
}
#include "common/image_data.h"

class VideoDemuxer {
public:
  VideoDemuxer() = default;
  ~VideoDemuxer();
  virtual bool init(std::string video_file_name);
  virtual ImageData replay(int frame_id);
  virtual void clear() {}
  int width() { return width_; }
  int height() { return height_; }
  AVCodecParameters *codecParameters();
  const std::string &fileName() const { return video_file_name_; }
private:
  using CacheContainer = std::vector<ImageData>;
  bool openFile();
  virtual void recacheFrom(int frame_id);
  virtual bool inCachedBuffer(int frame_id, const CacheContainer &);

 protected:
  std::string video_file_name_;
  int width_ = 0;
  int height_ = 0;
  AVFormatContext *format_ctx_{nullptr};

 private:
  int stream_index_ = 0; // video stream num
  AVPacket pkt_;

  CacheContainer cached_frames_;
  std::future<CacheContainer> cache_future_;

  AVCodecID codec_id_ = AV_CODEC_ID_NONE;

  const int kMaxCachedBufSize = 500;
  // Once the size of cached_frames is less than kMinRecacheBufSize, only
  // recache the buffer once the buffer reaches its end.
  const int kMinRecacheBufSize = 200;
  std::mutex mutex_;
};
#endif // IMAGEDEMUXER_H
