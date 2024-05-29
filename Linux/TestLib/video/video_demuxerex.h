/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2024-04-22 17:57:46
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:28:50
 * @FilePath: /data-engine/modules/video/video_demuxerex.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef IMAGEDEMUXER_EX_H
#define IMAGEDEMUXER_EX_H
#include <functional>
#include <future>
#include <map>
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
#include "ffmpeg_decoder.h"
#include "video_demuxer.h"
#include "avilib.h"

class VideoDemuxerEx  : public VideoDemuxer{
public:
  VideoDemuxerEx();
  ~VideoDemuxerEx();
  virtual bool init(std::string video_file_name);
  virtual ImageData replay(int frame_id);
  virtual void clear();

private:
  bool openFile();
  void recacheFrom(int frame_id);
  void  recacheFrom2(int frame_id);
  bool inCachedBuffer(int frame_id, const std::map<int, ImageData> &);
  virtual ImageData readFromFile(int frame_id);
  
  
  //virtual bool inCachedBuffer(int frame_id, const std::map<int, ImageData*> &);
  int stream_index_ = 0; // video stream num
  AVPacket pkt_;

  std::map<int, ImageData> cached_frames_;
  std::mutex m_lock_cachedframes;
  std::future<std::map<int, ImageData>> cache_future_;
  
  std::atomic<bool> m_bloading = false;
  std::atomic_int32_t m_loading_frameid = 0;

  AVCodecID codec_id_ = AV_CODEC_ID_NONE;

  FFmpegDecoder m_decoder;
  DecodeSemaphore m_semaphore;

  std::mutex m_lockread;
  //const int kMaxCachedBufSize = 500;
  // Once the size of cached_frames is less than kMinRecacheBufSize, only
  // recache the buffer once the buffer reaches its end.
  const int kMinRecacheBufSize = 40;

  avi_t* m_pavif = nullptr;
  char *m_pframebuffer = nullptr;

};
#endif // IMAGEDEMUXER_H
