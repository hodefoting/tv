// XXX: trim includes
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <libgen.h>
#include <sys/time.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <sys/mman.h>
#include <stdint.h>
#include <assert.h>
#include "tv.h"

#define HAVE_JPEG
#define HAVE_PNG

#ifdef HAVE_JPEG

#include <jpeglib.h>

unsigned char *jpeg_load (const char *filename,
                          int        *width,
                          int        *height,
                          int        *stride)
{ /* 96% of this code is the libjpeg example decoding code */
  unsigned char *retbuf = NULL;
  struct jpeg_decompress_struct cinfo;
  FILE * infile;
  JSAMPARRAY buffer;
  int row_stride;
  struct jpeg_error_mgr pub;
  if ((infile = fopen(filename, "rb")) == NULL) {
    fprintf(stderr, "can't open %s\n", filename);
    return 0;
  }
  cinfo.err = jpeg_std_error(&pub);
  jpeg_create_decompress(&cinfo);
  cinfo.quantize_colors = FALSE;
  cinfo.out_color_space = JCS_RGB;
  jpeg_stdio_src(&cinfo, infile);
  (void) jpeg_read_header(&cinfo, TRUE);
  (void) jpeg_start_decompress(&cinfo);
  row_stride = cinfo.output_width * cinfo.output_components;
  buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  /* XXX: need a bit more, jpeg lib overwrites by some bytes.. */

  retbuf = malloc (cinfo.output_width * (cinfo.output_height + 1) * 4);
  *width = cinfo.output_width;
  *height = cinfo.output_height;
  if (stride) *stride = cinfo.output_width * 4;
  while (cinfo.output_scanline < cinfo.output_height) {
	int x;
    (void) jpeg_read_scanlines(&cinfo, buffer, 1);
    for (x = 0; x < cinfo.output_width; x++)
      {
         int r = buffer[0][x * 3 + 0];
         int g = buffer[0][x * 3 + 1];
         int b = buffer[0][x * 3 + 2];
         retbuf[(cinfo.output_scanline * cinfo.output_width + x) * 4 + 0] = r;
         retbuf[(cinfo.output_scanline * cinfo.output_width + x) * 4 + 1] = g;
         retbuf[(cinfo.output_scanline * cinfo.output_width + x) * 4 + 2] = b;
         retbuf[(cinfo.output_scanline * cinfo.output_width + x) * 4 + 3] = 255;
      }
  }
  (void) jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(infile);
  return retbuf;
}
#endif

#ifdef HAVE_PNG
#include <png.h>

unsigned char *png_load (const char *filename, int *rw, int *rh, int *rs) {
  FILE *fp = fopen(filename, "rb");
  unsigned char ** scanlines;
  unsigned char *ret_buf = NULL;
  int i;
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                           NULL, NULL, NULL);
  if(!png)
    return NULL;

  png_infop info = png_create_info_struct(png);
  if(!info)
    return NULL;

  if(setjmp (png_jmpbuf (png)))
    return NULL;

  png_init_io (png, fp);
  png_read_info(png, info);
  *rw = png_get_image_width (png, info);
  *rh = png_get_image_height (png, info);
  if (rs) *rs = png_get_image_width (png, info) * 4;

  png_set_gray_to_rgb (png);
  png_set_strip_16 (png);
  png_set_palette_to_rgb (png);
  png_set_tRNS_to_alpha (png);
  png_set_expand_gray_1_2_4_to_8 (png);
  png_set_filler (png, 0xFF, PNG_FILLER_AFTER);

  png_read_update_info (png, info);

  ret_buf = malloc(*rw * *rh * 4);
  scanlines = (png_bytep*)malloc(sizeof(png_bytep) * *rh);
  for(i = 0; i < *rh; i++)
    scanlines[i] = (png_byte*)&ret_buf[*rw * 4 * i];
  png_read_image(png, scanlines);
  free (scanlines);
  png_destroy_read_struct (&png, &info, NULL);
  fclose(fp);
  return ret_buf;
}
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

unsigned char *
image_load (const char *path,
            int        *width,
            int        *height,
            int        *stride)
{
#ifdef HAVE_JPEG
  if (strstr (path, ".jpg") ||
      strstr (path, ".jpeg") ||
      strstr (path, ".JPG"))
  {
	 return jpeg_load (path, width, height, stride);
  }
#endif

#ifdef HAVE_PNG
  if (strstr (path, ".png") ||
      strstr (path, ".PNG"))
  {
	 return png_load (path, width, height, stride);
  }
#endif

  if (strstr (path, ".tga") ||
      strstr (path, ".pgm") ||
      strstr (path, ".gif") ||
      strstr (path, ".GIF") ||
      strstr (path, ".tiff") ||
      strstr (path, ".bmp") ||
      strstr (path, ".tiff") ||
      strstr (path, ".png") ||
      strstr (path, ".PNG") ||
      strstr (path, ".jpg") ||
      strstr (path, ".jpeg") ||
      strstr (path, ".JPG"))
    return stbi_load (path, width, height, stride, 4);
  return NULL;
}

static uint16_t gamma_to_linear[256];
static uint8_t  linear_to_gamma[65536];

static inline void init_gamma_tables (void)
{
  static int done = 0;
  if (done)
    return;
#ifdef USE_OPEN_MP
  #pragma omp for
#endif
  for (int i = 0; i < 256; i++)
    {
      float gamma = i / 255.0f;
      float linear;
      if (gamma > 0.04045f)
        linear = powf ((gamma+ 0.055f) / 1.055f, 2.4f);
      else
        linear = gamma/ 12.92;
      gamma_to_linear[i]= linear* 65535.0f + 0.5f;
    }
#ifdef USE_OPEN_MP
  #pragma omp for
#endif
  for (int i = 0; i < 65536; i++)
    {
      float linear= i / 65535.0f;
      float gamma;
      if (linear > 0.003130804954f)
        gamma = 1.055f * powf (linear, (1.0f/2.4f)) - 0.055f;
      else
        gamma = 12.92f * linear;
      linear_to_gamma[i]=gamma * 255.0f + 0.5f;
    }
}

void resample_image (const unsigned char *image,
                     int                  image_w,
                     int                  image_h,
                     unsigned char       *rgba,
                     int                  outw,
                     int                  outh,
                     int                  outs,
                     float                x_offset,
                     float                y_offset,
                     float                factor,
                     float                aspect,
                     int                  rotate,
                     int                  linear)
{
  int y, x;
  if (linear)
    init_gamma_tables ();

#ifdef USE_OPEN_MP
  #pragma omp for private(y)
#endif
  for (y = 0; y < outh; y++)
  {
    int i = y * outs;
    for (x = 0; x < outw; x ++)
    {
      int v = 0;
      int q0 = x     * factor + x_offset;
      int q1 = (x+1) * factor + x_offset;
      int accumulated[4] = {0,0,0,0};
      int got_coverage = 0;
      int z0;
      int z1;

      z0 = (y + v) * factor * aspect + y_offset;
      z1 = (y + v + 1) * factor * aspect + y_offset;
            
      int offset;
      switch (rotate)
      {
        default:
        case 0:
          offset = (int)((z0) * image_w + q0)*4;
  
          if (z0 < image_h &&
              q0 < image_w &&
              z0 >= 0 &&
              q0 >= 0)
              got_coverage = image[offset+3]>127;
            break;
        case 90:
          offset = (int)((image_h-q0) * image_w + z0)*4;
          if (q0 < image_h &&
              z0 < image_w &&
              q0 >= 0 &&
              z0 >= 0)
            got_coverage = image[offset+3]>127;
            break;
      }

      int c = 0;
      if (got_coverage)
        {
          int z, q;
          int offset2;

          if (q1 == q0) q1 = q0+1;
          if (z1 == z0) z1 = z0+1;
          if (q1 >= image_w) q1--;
          if (z1 >= image_h) z1--;

          if (linear)
          {
              for (q = q0; q<q1; q++)
                for (z = z0; z<z1; z++)
                  {
                    offset2 = offset + ((z-z0) * image_w + (q-q0))  * 4;
                    accumulated[0] += gamma_to_linear[image[offset2 + 0]];
                    accumulated[1] += gamma_to_linear[image[offset2 + 1]];
                    accumulated[2] += gamma_to_linear[image[offset2 + 2]];
                    accumulated[3] += image[offset2 + 3];
                    c++;
                  }
          }
          else
          {
          switch (rotate)
          {
            case 90:
#if 0
              for (q = q0; q<q1; q++)
                for (z = z0; z<z1; z++)
                {
                  offset2 = offset + ((q0-q) * image_w + (z-z0))  * 4;
                  accumulated[0] += image[offset2 + 0];
                  accumulated[1] += image[offset2 + 1];
                  accumulated[2] += image[offset2 + 2];
                  accumulated[3] += image[offset2 + 3];
                  c++;
                }
#endif
              break;
            case 0:
            default:
              for (q = q0; q<q1; q++)
                for (z = z0; z<z1; z++)
                  {
                    offset2 = offset + ((z-z0) * image_w + (q-q0))  * 4;
                    accumulated[0] += image[offset2 + 0];
                    accumulated[1] += image[offset2 + 1];
                    accumulated[2] += image[offset2 + 2];
                    accumulated[3] += image[offset2 + 3];
                    c++;
                  }
              break;
          }
          }
        }

      if (linear)
      {
      switch (c)
      {
        case 0:
          break;
        case 1:
          rgba[i + 0] = linear_to_gamma[accumulated[0]];
          rgba[i + 1] = linear_to_gamma[accumulated[1]];
          rgba[i + 2] = linear_to_gamma[accumulated[2]];
          rgba[i + 3] = accumulated[3];
          break;
        case 4:
          rgba[i + 0] = linear_to_gamma[accumulated[0]/4];
          rgba[i + 1] = linear_to_gamma[accumulated[1]/4];
          rgba[i + 2] = linear_to_gamma[accumulated[2]/4];
          rgba[i + 3] = accumulated[3]/4;
          break;
        case 8:
          rgba[i + 0] = linear_to_gamma[accumulated[0]/8];
          rgba[i + 1] = linear_to_gamma[accumulated[1]/8];
          rgba[i + 2] = linear_to_gamma[accumulated[2]/8];
          rgba[i + 3] = accumulated[3]/8;
          break;
        default:
          rgba[i + 0] = linear_to_gamma[accumulated[0]/c];
          rgba[i + 1] = linear_to_gamma[accumulated[1]/c];
          rgba[i + 2] = linear_to_gamma[accumulated[2]/c];
          rgba[i + 3] = accumulated[3]/c;
          break;
      }
      }
      else
      switch (c)
      {
        case 0:
          break;
        case 1:
          rgba[i + 0] = accumulated[0];
          rgba[i + 1] = accumulated[1];
          rgba[i + 2] = accumulated[2];
          rgba[i + 3] = accumulated[3];
          break;
        case 4:
          rgba[i + 0] = accumulated[0]/4;
          rgba[i + 1] = accumulated[1]/4;
          rgba[i + 2] = accumulated[2]/4;
          rgba[i + 3] = accumulated[3]/4;
          break;
        case 8:
          rgba[i + 0] = accumulated[0]/8;
          rgba[i + 1] = accumulated[1]/8;
          rgba[i + 2] = accumulated[2]/8;
          rgba[i + 3] = accumulated[3]/8;
          break;
        default:
          rgba[i + 0] = accumulated[0]/c;
          rgba[i + 1] = accumulated[1]/c;
          rgba[i + 2] = accumulated[2]/c;
          rgba[i + 3] = accumulated[3]/c;
          break;
      }
      i+= 4;
    }
  }
}

int write_jpg (char const *filename,
               int w, int h, int comp, const void *data,
               int stride_in_bytes)
{
  const uint8_t *cdata = data;
  FILE *file = fopen (filename, "wb");
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr       jerr;

  if (!file)
  {
    return -1;
  }
  cinfo.err = jpeg_std_error (&jerr);
  jpeg_create_compress (&cinfo);
  jpeg_stdio_dest (&cinfo, file);
  cinfo.image_width = w;
  cinfo.image_height = h;
  cinfo.input_components = 4;
  cinfo.in_color_space = JCS_EXT_RGBX;

  jpeg_set_defaults (&cinfo);
  jpeg_set_quality (&cinfo, 75, TRUE);
  jpeg_start_compress (&cinfo, TRUE);

  while (cinfo.next_scanline < cinfo.image_height)
  {
    JSAMPROW rp = (JSAMPROW) cdata + cinfo.next_scanline * stride_in_bytes;
    jpeg_write_scanlines (&cinfo, &rp, 1);
  }
  jpeg_finish_compress (&cinfo);

  fclose (file);

  jpeg_destroy_compress (&cinfo);
  return 0;
}
