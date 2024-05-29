#include "video_demuxer.h"

#include <iostream>

#include "common/easylogging++.h"
#include "common/utils.h"
#include "video/video_utils.h"

bool VideoDemuxer::init(std::string video_file_name) {
  video_file_name_ = video_file_name;

  if (!openFile()) {
    LOG(ERROR) << "Failed to open file " << video_file_name_;
    return false;
  }

  recacheFrom(1);

  return true;
}

ImageData VideoDemuxer::replay(int frame_id) {
  // Check if the frame is include in the cached_frames.
  if (inCachedBuffer(frame_id, cached_frames_)) {
    LOG(DEBUG) << "The input " << frame_id << " is in the cached range!";
    auto last_frame_id = cached_frames_[cached_frames_.size() - 1].frame_id;
    if (static_cast<int>(cached_frames_.size()) > kMinRecacheBufSize &&
        last_frame_id - frame_id <
            static_cast<int32_t>(cached_frames_.size() / 2)) {
      LOG(DEBUG) << "Need recache!last_frame_id:" << last_frame_id
                 << " frame_id:" << frame_id
                 << " cached_frames_.size:" << cached_frames_.size();
      recacheFrom(last_frame_id + 1);
    }

    return cached_frames_.at(frame_id - cached_frames_[0].frame_id);
  }

  // Check if the frame is in the cache_future.
  if (cache_future_.valid()) {
    LOG(INFO) << "The cache future is valid! Will check if the frame "
              << frame_id << " is included in the cache future";
    auto cached_data = cache_future_.get();
    if (inCachedBuffer(frame_id, cached_data)) {
      LOG(INFO) << "the frame " << frame_id
                << " is included in the cache future";
      cached_frames_ = std::move(cached_data);
      return cached_frames_.at(frame_id - cached_frames_[0].frame_id);
    }
  }

  LOG(INFO) << "The frame " << frame_id
            << " is not included in neither cached_buf_ nor the cached_future! "
               "Will recache!";
  recacheFrom(frame_id);
  if (!cache_future_.valid()) {
    LOG(ERROR) << "Recache failed with frame_id" << frame_id;
    return ImageData();
  }

  cached_frames_ = cache_future_.get();
  if (cached_frames_.empty()) {
    LOG(ERROR) << "Recache failed with frame_id" << frame_id;
    return ImageData();
  }

  LOG(INFO) << "Get the cached_frames from future successfully!size:"
            << cached_frames_.size() << " from:" << cached_frames_[0].frame_id
            << " to " << cached_frames_[cached_frames_.size() - 1].frame_id;

  if (!inCachedBuffer(frame_id, cached_frames_)) {
    LOG(ERROR) << "The recached frames_ doesn't included the frame_id "
               << frame_id;
    return ImageData();
  }

  return cached_frames_.at(frame_id - cached_frames_[0].frame_id);
}

AVCodecParameters* VideoDemuxer::codecParameters() {
  if (format_ctx_ && format_ctx_->streams[stream_index_])
    return format_ctx_->streams[stream_index_]->codecpar;
  return nullptr;
}

VideoDemuxer::~VideoDemuxer() {
  std::unique_lock<std::mutex> lock(mutex_);
  avformat_close_input(&format_ctx_);
  format_ctx_ = nullptr;
  lock.unlock();
}

void VideoDemuxer::recacheFrom(int frame_id) {
  if (cache_future_.valid()) {
    LOG(DEBUG) << "The future contains the cached data! No recache needed";
    return;
  }

  cache_future_ = std::async(std::launch::async, [this, frame_id]() {
    std::unique_lock<std::mutex> lock(mutex_);
    auto now = TimeUtil::unixNowInUs();
    int64_t seek_pts_time = 512 * frame_id;
    if (!format_ctx_) {
      return CacheContainer();
    }
    auto ret = av_seek_frame(format_ctx_, stream_index_, seek_pts_time,
                             AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
      LOG(ERROR) << "av_seek_frame in " << video_file_name_
                 << " failed!ret:" << ret;
      return CacheContainer();
    }

    int count = 0;
    CacheContainer cached_buf;
    while (true) {
      auto read_res = av_read_frame(format_ctx_, &pkt_);
      if (read_res < 0) {
        LOG(INFO) << "av read frame return non zero value!ret:"
                  << videoutils::printAvError(read_res);
        break;
      }

      if (pkt_.stream_index == stream_index_) {
        auto frame_id = pkt_.pts / 512;
        ImageData image;
        image.frame_id = frame_id;
        image.width = width_;
        image.height = height_;
        // TODO currently there are only two codec types. need refactor
        // later.
        if (pkt_.flags & AV_PKT_FLAG_KEY)  // is keyframe
        {
          image.bkeyframe = true;
        }
        image.codec_type =
            (codec_id_ == AV_CODEC_ID_H264 ? CodecType::H264 : CodecType::H265);
        image.data.resize(pkt_.size);
        memcpy(image.data.data(), pkt_.data, pkt_.size);
        av_packet_unref(&pkt_);

        cached_buf.push_back(std::move(image));
        count++;

        if (count == kMaxCachedBufSize) {
          break;
        }
      }
    }
    lock.unlock();
    LOG(INFO) << "recache elapsed " << (TimeUtil::unixNowInUs() - now) / 1.0
              << " ms";
    return cached_buf;
  });
}

bool VideoDemuxer::openFile() {
  // av_register_all();

  av_init_packet(&pkt_);
  // allocate format context
  format_ctx_ = avformat_alloc_context();
  if (format_ctx_ == nullptr) {
    LOG(ERROR) << "Failed to alloc the format context.";
    return false;
  }

  format_ctx_->probesize = 100 * 1024 * 1024;  // 100MB
  format_ctx_->max_analyze_duration = 100 * AV_TIME_BASE; // 100s
  // trying to open the input file stream
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

  LOG(INFO) << "The " << video_file_name_ << " is initialized successfully!";
  return true;
}

bool VideoDemuxer::inCachedBuffer(int frame_id,
                                  const CacheContainer &cached_data) {
  if (cached_data.empty()) {
    return false;
  }

  auto first_frame_id = cached_data[0].frame_id;
  if (frame_id < first_frame_id) {
    return false;
  }

  return static_cast<std::size_t>(frame_id - first_frame_id) <
         cached_data.size();
}

