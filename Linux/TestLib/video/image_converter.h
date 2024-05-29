
#ifndef VIDEO_IMAGE_CONVERTER_H_
#define VIDEO_IMAGE_CONVERTER_H_

class ImageData;
class ImageConverter final {
public:
  static ImageData convertToYuv420(ImageData &&image);
};


#endif /* VIDEO_IMAGE_CONVERTER_H_ */
