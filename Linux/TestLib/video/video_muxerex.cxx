#include "video_muxerex.h"
#include "common/string_util.h"
#include "common/utils.h"
#include "jpeglib.h"
#include "turbojpeg.h"


void jpgmem_to_jpgfile(char *jpg_file, unsigned char *jpg, int size) {
  FILE *fp = fopen(jpg_file, "wb+");
  if (fp == NULL)
    return;
  fwrite(jpg, size, 1, fp);
  fclose(fp);
}


int get_jpeg_compress_data2(const unsigned char* data, int width, int height, int pixelFormat, unsigned char** jpegBuf, unsigned long* jpegSize, int jpegSubsamp, int jpegQual, int flags)
{
	tjhandle handle = tjInitCompress();
	int ret = tjCompress2(handle, data, width, 0, height, pixelFormat, jpegBuf, jpegSize, jpegSubsamp, jpegQual, flags);
	tjDestroy(handle);
 
	return 0;
}

void VideoMuxerEx::RGB_TO_JPEG(unsigned char *rgb, int image_width,
                               int image_height, int quality /*= 90*/,
                               unsigned char *poutdata, size_t &outsize) {
  JSAMPLE *image_buffer = (JSAMPLE *)rgb;
  /* This struct contains the JPEG compression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   * It is possible to have several such structures, representing multiple
   * compression/decompression processes, in existence at once.  We refer
   * to any one struct (and its associated working data) as a "JPEG object".
   */
  struct jpeg_compress_struct cinfo;
  /* This struct represents a JPEG error handler.  It is declared separately
   * because applications often want to supply a specialized error handler
   * (see the second half of this file for an example).  But here we just
   * take the easy way out and use the standard error handler, which will
   * print a message on stderr and call exit() if compression fails.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
  struct jpeg_error_mgr jerr;
  /* More stuff */
  JSAMPROW row_pointer[1]; /* pointer to JSAMPLE row[s] */
  int row_stride;          /* physical row width in image buffer */

  /* Step 1: allocate and initialize JPEG compression object */

  /* We have to set up the error handler first, in case the initialization
   * step fails.  (Unlikely, but it could happen if you are out of memory.)
   * This routine fills in the contents of struct jerr, and returns jerr's
   * address which we place into the link field in cinfo.
   */
  cinfo.err = jpeg_std_error(&jerr);
  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress(&cinfo);

  jpeg_mem_dest(&cinfo, &poutdata, &outsize);

  /* Step 3: set parameters for compression */

  /* First we supply a description of the input image.
   * Four fields of the cinfo struct must be filled in:
   */
  cinfo.image_width = image_width; /* image width and height, in pixels */
  cinfo.image_height = image_height;
  cinfo.input_components = 3;     /* # of color components per pixel */
  cinfo.in_color_space = JCS_RGB; /* colorspace of input image */
  /* Now use the library's routine to set default compression parameters.
   * (You must set at least cinfo.in_color_space before calling this,
   * since the defaults depend on the source color space.)
   */
  jpeg_set_defaults(&cinfo);
  /* Now you can set any non-default parameters you wish to.
   * Here we just illustrate the use of quality (quantization table) scaling:
   */
  jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

  /* Step 4: Start compressor */

  /* TRUE ensures that we will write a complete interchange-JPEG file.
   * Pass TRUE unless you are very sure of what you're doing.
   */
  jpeg_start_compress(&cinfo, TRUE);

  /* Step 5: while (scan lines remain to be written) */
  /*           jpeg_write_scanlines(...); */

  /* Here we use the library's state variable cinfo.next_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   * To keep things simple, we pass one scanline per call; you can pass
   * more if you wish, though.
   */
  row_stride = image_width * 3; /* JSAMPLEs per row in image_buffer */
  while (cinfo.next_scanline < cinfo.image_height) {
    /* jpeg_write_scanlines expects an array of pointers to scanlines.
     * Here the array is only one element long, but you could pass
     * more than one scanline at a time if that's more convenient.
     */
    row_pointer[0] = &image_buffer[cinfo.next_scanline * row_stride];
    (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }
  /* Step 6: Finish compression */

  jpeg_finish_compress(&cinfo);

  /* Step 7: release JPEG compression object */

  if (outsize > 0) {
    AVI_write_frame(m_pavi, (char *)poutdata, outsize, 1);
  }

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_compress(&cinfo);

  /* And we're done! */
}

bool VideoMuxerEx::init(std::string filename, int width, int height) {
  bool b = VideoMuxer::init(filename, width, height);
  if (!b) {
    return b;
  }
  std::string oldstr = ".mp4";
  std::string newstr = ".avi";
  std::string strpath = StringUtil::ReplaceAll(filename, oldstr, newstr);
  if (FileUtil::isFileExisted(strpath)) {
    m_bneedavi = false;
    return true;
  }
  auto avihand = AVI_open_output_file(strpath.c_str());
  char str[] = "mjpg";
  AVI_set_video(avihand, width, height, 30, str);

  m_pavi = avihand;
  m_ffmpeg.SetAvcodecParameters(this->out_stream_->codecpar);
  m_ffmpeg.Init(width, height);

  m_ptempdata = new unsigned char[width * height * 4];
  return true;
}

VideoMuxerEx::~VideoMuxerEx() {
  if (m_ptempdata) {
    delete[] m_ptempdata;
    m_ptempdata = nullptr;
  }

  if (m_pavi) {
    AVI_close(m_pavi);
    m_pavi = nullptr;
  }
}

void VideoMuxerEx::writeFrame(unsigned char *buffer, int size) {
  VideoMuxer::writeFrame(buffer, size);
  if (!m_bneedavi) {
    return;
  }
  int outid = 0;
  int outsize = 0;
  m_lock.lock();
  uint8_t *pdata = m_ffmpeg.Decode(0, buffer, size, outid, outsize);
  m_lock.unlock();
  if (pdata && outsize > 0) {

    size_t jpegsize = 0;
    int width = m_ffmpeg.GetDecodeWidth();
    int height = m_ffmpeg.GetDecodeHeight();

    std::unique_ptr<unsigned char[]> data2(new unsigned char[outsize]);
    auto p = data2.get();
    get_jpeg_compress_data2(pdata,width,height,TJPF_BGR,&p,&jpegsize,TJSAMP_420,30,0);    
  if (jpegsize > 0) {
    AVI_write_frame(m_pavi, (char *)p, jpegsize, 1);
  }    
    //RGB_TO_JPEG(pdata, width, height, 30, m_ptempdata, jpegsize);
    // if (jpegsize > 0) {
    //   AVI_write_frame(m_pavi, (char *)m_ptempdata, jpegsize, 1);
    // }
  }
}

