#ifndef VIDEO_DEMUXER_MANAGER_CPP
#define VIDEO_DEMUXER_MANAGER_CPP
#include "video_demuxer_manager.h"
#include "common/video_file_name.h"
#include "video_demuxer.h"
#include "video_demuxerex.h"
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;
VideoDemuxerManager *VideoDemuxerManager::GetInstance() {
  static VideoDemuxerManager g_manager;
  return &g_manager;
}
VideoDemuxerManager::VideoDemuxerManager(/* args */) {}

VideoDemuxerManager::~VideoDemuxerManager() {}

bool VideoDemuxerManager::init(std::string strpath,
                               std::string strfile_extension) {
  video_demuxers_.clear();
  strpath_ = strpath;
  std::vector<std::string> mp4_files;
  for (auto &dir_entry : fs::directory_iterator{strpath}) {
    auto path = dir_entry.path();
    if (path.extension() == strfile_extension) {
      mp4_files.push_back(path.string());
    }
  }

  if (mp4_files.empty()) {
    return false;
  }
  for (auto f : mp4_files) {
    auto pure_file_name = fs::path(f).filename().string();
    auto camera_id = VideoFileName::getCameraId(pure_file_name);
    if (camera_id < 0) {
      continue;
    }

    std::shared_ptr<VideoDemuxer> demuxer = nullptr;
    if (strfile_extension == ".avi") {
      demuxer = std::make_shared<VideoDemuxerEx>();
    } else if (strfile_extension == ".mp4") {
      demuxer = std::make_shared<VideoDemuxer>();
    }

    if (!demuxer->init(f)) {
      continue;
    }
    video_demuxers_.insert({camera_id, std::move(demuxer)});
  }

  return true;
}

std::shared_ptr<VideoDemuxer> VideoDemuxerManager::find(int camera_id) {
  auto finditer = video_demuxers_.find(camera_id);
  if (finditer != video_demuxers_.end()) {
    return finditer->second;
  }

  return nullptr;
}

std::vector<int> VideoDemuxerManager::cameraIds() {
  std::vector<int> list;
  for (const auto &[k, v] : video_demuxers_) {
    list.push_back(k);
  }
  return list;
}

bool VideoDemuxerManager::reload(std::string strfile_extension) {
    video_demuxers_.clear();

  std::vector<std::string> mp4_files;
  for (auto &dir_entry : fs::directory_iterator{strpath_}) {
    auto path = dir_entry.path();
    if (path.extension() == strfile_extension) {
      mp4_files.push_back(path.string());
    }
  }

  if (mp4_files.empty()) {
    return false;
  }
  for (auto f : mp4_files) {
    auto pure_file_name = fs::path(f).filename().string();
    auto camera_id = VideoFileName::getCameraId(pure_file_name);
    if (camera_id < 0) {
      continue;
    }

    std::shared_ptr<VideoDemuxer> demuxer = nullptr;
    if (strfile_extension == ".avi") {
      demuxer = std::make_shared<VideoDemuxerEx>();
    } else if (strfile_extension == ".mp4") {
      demuxer = std::make_shared<VideoDemuxer>();
    }

    if (!demuxer->init(f)) {
      continue;
    }
    video_demuxers_.insert({camera_id, std::move(demuxer)});
  }

  return true;    
}

#endif