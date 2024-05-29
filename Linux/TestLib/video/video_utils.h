/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2023-07-04 13:33:03
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:31:03
 * @FilePath: /data-engine/modules/video/video_utils.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef VIDEO_VIDEO_UTILS_H_
#define VIDEO_VIDEO_UTILS_H_

#include <string>

extern "C" {
#include "libavutil/error.h"
}

class ImageDistortInfo;
namespace videoutils {
bool isKeyFrame(unsigned char *pbuf, int buf_size);
std::string serializeDistortInfo(const ImageDistortInfo &info);
bool deserializeDistortInfo(const std::string &json_str,
                            ImageDistortInfo &info);

std::string printAvError(int ret);
}

#endif /* VIDEO_VIDEO_UTILS_H_ */
