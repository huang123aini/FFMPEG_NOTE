#include "video_demuxerex.h"

#include <iostream>

#include "avilib.h"
#include "common/easylogging++.h"
#include "common/taskqueue.h"
#include "common/utils.h"
#include "video/video_utils.h"

bool VideoDemuxerEx::init(std::string video_file_name) {
  m_loading_frameid = 0;
  video_file_name_ = video_file_name;

  if (!openFile()) {
    LOG(ERROR) << "Failed to open file " << video_file_name_;
    return false;
  }

  // recacheFrom(1);

  return true;
}

void VideoDemuxerEx::clear() {
  m_lock_cachedframes.lock();
  cached_frames_.clear();
  m_lock_cachedframes.unlock();
}

ImageData VideoDemuxerEx::replay(int frame_id) {

auto image = readFromFile(frame_id);
return image;
  // Check if the frame is include in the cached_frames.
  m_lock_cachedframes.lock();
  auto iter = cached_frames_.begin();
  for (; iter != cached_frames_.end();) {
    if (iter->first < frame_id) {

      // delete []iter->second->p_rgbdata;
      // delete iter->second;
      iter = cached_frames_.erase(iter);
    } else {
      break;
    }
  }

  if (inCachedBuffer(frame_id, cached_frames_)) {
    auto rdata = cached_frames_.at(frame_id);
    cached_frames_.erase(frame_id);

    if (cached_frames_.size() > 0) {
      auto lastitem = cached_frames_.rbegin();
      auto last_frame_id = lastitem->second.frame_id;
      m_lock_cachedframes.unlock();
      int frames = AVI_video_frames(m_pavif);
      if (static_cast<int>(cached_frames_.size()) < kMinRecacheBufSize &&
          frame_id < frames && !m_bloading) {
        m_loading_frameid = last_frame_id + 1;
        recacheFrom2(last_frame_id + 1);
      }
    }
    m_lock_cachedframes.unlock();
    return rdata;
  }



  m_lock_cachedframes.unlock();
  // m_lockread.lock();
  // auto image = readFromFile(frame_id);
  // m_lockread.unlock();
  m_loading_frameid = frame_id + 1;
  if (!m_bloading) {
    recacheFrom2(m_loading_frameid);
  }
  return ImageData();
  // return nullptr;
  //return image;
}

ImageData VideoDemuxerEx::readFromFile(int frame_id) {

  auto curTime = std::chrono::steady_clock::now();
  AVI_set_video_position((avi_t *)m_pavif, frame_id);

  int keyFrame = 0;
  int frameSize = AVI_read_frame((avi_t *)m_pavif, m_pframebuffer, &keyFrame);
  auto curTime2 = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(curTime2 - curTime);
  if (frameSize > 0) {
    int outsize = frameSize;
    int outid = 0;
    ImageData image;
    image.frame_id = outid;
    image.width = m_decoder.GetWidth();
    image.height = m_decoder.GetHeight();
    image.codec_type = CodecType::JPEG;
    image.data.resize(outsize);
    memcpy(image.data.data(), m_pframebuffer, outsize);
    return image;
  }
  return ImageData();
}

VideoDemuxerEx::~VideoDemuxerEx() {
  avformat_close_input(&format_ctx_);
  avformat_free_context(format_ctx_);
  format_ctx_ = nullptr;
  delete[] m_pframebuffer;
  if (m_pavif) {
    AVI_close(m_pavif);
    m_pavif = nullptr;
  }
}

VideoDemuxerEx::VideoDemuxerEx() { m_pframebuffer = new char[300 * 10240]; }

bool VideoDemuxerEx::inCachedBuffer(
    int frame_id, const std::map<int, ImageData> &cached_data) {
  if (cached_data.empty()) {
    return false;
  }

  auto find = cached_data.find(frame_id);
  if (find != cached_data.end()) {
    return true;
  }
  return false;
}

void VideoDemuxerEx::recacheFrom2(int frame_id) {
  m_bloading = true;
  // std::cout << "recacheFrom in:" << this->video_file_name_.c_str() <<
  // std::endl;
  RunAddTaskQueue(video_file_name_, [this, frame_id]() {
    // std::cout << "beginrecach:" << this->video_file_name_.c_str() <<
    // std::endl;
    int count = kMinRecacheBufSize;
    std::vector<int> tempframeid;
    //this->m_lock_cachedframes.lock();
    for (int i = 0; i < count; i++) {
      // auto find = this->cached_frames_.find(frame_id + i);
      // if (find == this->cached_frames_.end()) {
      //   tempframeid.push_back(frame_id + i);
      // }
      int frame_id = m_loading_frameid + i;
     // m_lockread.lock();
      auto pimage = this->readFromFile(frame_id);
     // m_lockread.unlock();

      this->m_lock_cachedframes.lock();
      this->cached_frames_.insert(std::make_pair(frame_id, pimage));
      if (this->cached_frames_.find(frame_id) != this->cached_frames_.end()) {
        // this->m_lock_cachedframes.unlock();
        // break;
      }
      this->m_lock_cachedframes.unlock();
    }
    //this->m_lock_cachedframes.unlock();
    // for (auto i : tempframeid) {
    //   int frame_id = i;
    //   m_lockread.lock();
    //   auto pimage = this->readFromFile(frame_id);
    //   m_lockread.unlock();

    //   this->m_lock_cachedframes.lock();
    //   this->cached_frames_.insert(std::make_pair(frame_id, pimage));
    //   if (this->cached_frames_.find(frame_id) != this->cached_frames_.end()) {
    //     // this->m_lock_cachedframes.unlock();
    //     // break;
    //   }
    //   this->m_lock_cachedframes.unlock();
    // }
    m_bloading = false;
    // std::cout << "endrecach:" << this->video_file_name_.c_str() << std::endl;
  });

  // auto r = std::async(std::launch::async, [this, frame_id]() { return true; });
  // return r;
}

void VideoDemuxerEx::recacheFrom(int frame_id) {
  // if(m_loading_frameid == frame_id){
  //   return;
  // }

  // m_loading_frameid = frame_id;

  // RunAddTaskQueue(video_file_name_,[this,frame_id]() {
  //   int64_t seek_pts_time = 512 * frame_id;
  //   auto ret = av_seek_frame(format_ctx_, stream_index_, seek_pts_time,
  //                            AVSEEK_FLAG_BACKWARD);
  //   if (ret < 0) {
  //     return ;
  //   }

  //   int count = kMinRecacheBufSize;
  //   bool bcatch = false;

  //   std::cout << "addrecache:" << frame_id << " " << this->video_file_name_
  //   <<std::endl ; std::map<int,ImageData*> maptemp; while (true) {
  //     auto read_res = av_read_frame(format_ctx_, &pkt_);
  //     if (read_res < 0) {
  //       std::cout << "fuckbreak:" << frame_id << " " <<
  //       this->video_file_name_ <<std::endl ; break;
  //     }

  //     if (pkt_.stream_index == stream_index_) {
  //       auto pkt_frameid = pkt_.pts / 512;
  //       int outsize = 0;
  //       int outid = 0;
  //       auto *pdata =
  //           m_decoder.Decode(pkt_frameid, pkt_.data, pkt_.size, outid,
  //           outsize);
  //       av_packet_unref(&pkt_);
  //       if (pkt_frameid == frame_id) {
  //         bcatch = true;
  //       }
  //       if (pdata && bcatch && outsize > 0) {
  //         ImageData *image = new ImageData;
  //         image->frame_id = pkt_frameid;
  //         image->width = width_;
  //         image->height = height_;
  //         std::shared_ptr<char[]> pchar(new
  //         char[outsize],std::default_delete<char[]>());
  //         //image->p_rgbdata = new char[outsize];
  //         image->p_rgbdata = pchar;
  //         image->rgb_datasize = outsize;
  //         memcpy(image->p_rgbdata.get(), pdata, outsize);

  //         // maptemp.insert(std::make_pair(pkt_frameid, image));
  //         this->m_lock_cachedframes.lock();
  //         this->cached_frames_.insert(std::make_pair(pkt_frameid,image));
  //         if(this->cached_frames_.find(pkt_frameid+1) !=
  //         this->cached_frames_.end()){
  //           m_loading_frameid = 0;
  //           this->m_lock_cachedframes.unlock();
  //           break;
  //         }
  //         this->m_lock_cachedframes.unlock();
  //         // std::cout <<" frameid:" << pkt_frameid << " size:" << outsize;
  //         count--;
  //         if (count == 0) {
  //           m_loading_frameid = 0;

  //           break;
  //         }
  //       }
  //     }

  //   }

  //   // this->m_lock_cachedframes.lock();
  //   // this->cached_frames_.insert(maptemp.begin(),maptemp.end());
  //   // this->m_lock_cachedframes.unlock();
  // });
}

bool VideoDemuxerEx::openFile() {
  // av_register_all();

  m_pavif = AVI_open_input_file(video_file_name_.c_str(), 1);
  if (!m_pavif) {
    LOG(ERROR) << AVI_strerror();
    return false;
  }
  m_decoder.Init(m_pavif->width, m_pavif->height, false);
  width_ = m_pavif->width;
  height_ = m_pavif->height;
  return true;

  av_init_packet(&pkt_);

  // allocate format context
  format_ctx_ = avformat_alloc_context();
  if (format_ctx_ == nullptr) {
    LOG(ERROR) << "Failed to alloc the format context.";
    return false;
  }

  // trying to open the input file stream
  // format_ctx_->probesize = 10000000 * 8;
  // format_ctx_->pb = avio;
  auto ret =
      avformat_open_input(&format_ctx_, video_file_name_.c_str(), NULL, NULL);
  if (ret) {
    LOG(ERROR) << "Failed to open the file " << video_file_name_;
    return false;
  }

  avformat_find_stream_info(format_ctx_, nullptr);
  int video_stream = -1;
  for (unsigned int i = 0; i < format_ctx_->nb_streams; i++) {
    if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream = i;
      break;
    }
  }

  if (video_stream == -1) {
    LOG(ERROR) << "Didn't find the video stream in " << video_file_name_;
    return false;
  }

  stream_index_ = video_stream;

  auto pCodecCtxOrig = format_ctx_->streams[stream_index_]->codecpar;
  width_ = pCodecCtxOrig->width;
  height_ = pCodecCtxOrig->height;
  codec_id_ = pCodecCtxOrig->codec_id;

  m_decoder.Init(width_, height_);
  m_decoder.SetAvcodecParameters((void *)pCodecCtxOrig);
  LOG(INFO) << "The " << video_file_name_ << " is initialized successfully!";
  return true;
}

// bool VideoDemuxerEx::inCachedBuffer(
//     int frame_id, const std::map<int, ImageData> &cached_data) {
//   if (cached_data.empty()) {
//     return false;
//   }

//   auto find = cached_data.find(frame_id);
//   if (find != cached_data.end()) {
//     return true;
//   }
//   return false;
// }

