/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2024-01-09 16:10:41
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:27:59
 * @FilePath: /data-engine/modules/video/video_demuxer_manager.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef VIDEO_DEMUXER_MANAGER_H
#define VIDEO_DEMUXER_MANAGER_H
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>

class VideoDemuxer;
class VideoDemuxerManager {
public:
  static VideoDemuxerManager *GetInstance();
  /* data */
public:
  VideoDemuxerManager(/* args */);
  ~VideoDemuxerManager();

  bool init(std::string strpath, std::string strfile_extension = ".avi");
  bool reload(std::string strfile_extension = ".avi");
  
  std::shared_ptr<VideoDemuxer> find(int camera_id);

  std::vector<int> cameraIds();

private:
  std::unordered_map<int, std::shared_ptr<VideoDemuxer>> video_demuxers_;

  std::string strpath_;
};


#endif