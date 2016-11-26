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
#include "mrg-list.h"

typedef struct CachedImage
{
  char *path;
  int width;
  int height;
  char *data;
} CacheImage;

static int compute_size (int w, int h)
{
  return w * h * 4 + sizeof (CacheImage) + 1024;
}

static long image_cache_size = 0;
static int  image_cache_max_size_mb = 128;

static MrgList *image_cache = NULL;

static void free_image (void *data, void *foo)
{
  CacheImage *image = data;
  free (image->path);
  free (image->data);
  free (image);
}

static void trim_cache (void)
{
  while (image_cache_size > image_cache_max_size_mb * 1024 * 1024)
  {
    CacheImage *image;
    int item = mrg_list_length (image_cache);
    int w, h;
    item = random() % item;
    image = mrg_list_nth (image_cache, item)->data;
    w = image->width;
    h = image->height;
    mrg_list_remove (&image_cache, image);
    image_cache_size -= compute_size (w, h);
  }
}



#define HAVE_JPEG
#define HAVE_PNG
#ifdef  HAVE_JPEG

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

#if 1
unsigned char *
detect_mime_type (const char *path)
{
  unsigned char jpgsig[4]={0xff, 0xd8, 0xff, 0xe0};
  char tmpbuf[256+1];
  ssize_t length;
  int fd;
  fd = open (path, O_RDONLY);
  if (!fd)
    return "text/plan";
  length = read (fd, tmpbuf, 256);
  if (length <= 0)
    return "text/plain";
  close (fd);
  tmpbuf[length]=0;

  if (!memcmp(tmpbuf, "\211PNG\r\n\032\n", 8))
     return "image/png";
  else if (!memcmp(tmpbuf, jpgsig, 4))
     return "image/jpeg";

  if (strstr (path, ".tga") ||
      strstr (path, ".pgm") ||
      strstr (path, ".gif") ||
      strstr (path, ".GIF") ||
      strstr (path, ".tiff") ||
      strstr (path, ".bmp") ||
      strstr (path, ".tiff"))
    return "image/x";
}
#endif

unsigned char *
image_load (const char *path,
            int        *width,
            int        *height,
            int        *stride)
{
  const char *mime_type = detect_mime_type (path);

  if (!strcmp (mime_type, "image/jpeg"))
  {
#ifdef HAVE_JPEG
	return jpeg_load (path, width, height, stride);
#else
    return stbi_load (path, width, height, stride, 4);
#endif
  }
  else if (!strcmp (mime_type, "image/png"))
  {
#ifdef HAVE_PNG
	return png_load (path, width, height, stride);
#else
    return stbi_load (path, width, height, stride, 4);
#endif
  }
  else if (!strcmp (mime_type, "image/x"))
  {
    return stbi_load (path, width, height, stride, 4);
  }
  else
  {
    return NULL;
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
      int q0 = (x+0) * factor + x_offset;
      int q1 = (x+1) * factor + x_offset;
      int accumulated[4] = {0,0,0,0};
      int z0 = (y + v + 0) * factor * aspect + y_offset;
      int z1 = (y + v + 1) * factor * aspect + y_offset;
      int offset = (int)((z0) * image_w + q0)*4;
  
      if (z0 < image_h &&
          q0 < image_w &&
          z0 >= 0 &&
          q0 >= 0 &&
          image[offset+3]>127)
      {
        int count = 0;
        int z, q;
        int offset2;

        if (q1 == q0) q1 = q0+1;
        if (z1 == z0) z1 = z0+1;
        if (q1 >= image_w) q1 = image_w-1;
        if (z1 >= image_h) z1 = image_h-1;

        if (linear)
        {
          for (q = q0; q<q1; q++)
            for (z = z0; z<z1; z++)
              {
                offset2 = offset + ((z-z0) * image_w + (q-q0))  * 4;
                for (int c = 0; c < 3; c++)
                  accumulated[c] += gamma_to_linear[image[offset2 + c]];
                accumulated[3] += image[offset2 + 3];
                count++;
              }
        }
        else
        {
          for (q = q0; q<q1; q++)
            for (z = z0; z<z1; z++)
              {
                offset2 = offset + ((z-z0) * image_w + (q-q0))  * 4;
                for (int c = 0; c < 4; c++)
                  accumulated[c] += image[offset2 + c];
                count++;
              }
        }

        if (count)
        {
          if (linear)
          {
            for (int c = 0; c < 3; c++)
              rgba[i + c] = linear_to_gamma[accumulated[c]/count];
            rgba[i + 3] = accumulated[3]/count;
          }
          else
          {
            for (int c = 0; c < 4; c++) rgba[i + c] = accumulated[c]/count;
          }
        }
      }
      i+= 4;
    }
  }
}

void mrg_set_image_cache_mb (int new_max_size)
{
  image_cache_max_size_mb = new_max_size;
  trim_cache ();
}

int mrg_get_image_cache_mb (void)
{
  return image_cache_max_size_mb;
}

unsigned char *cached_image (const char *path,
                             int *width,
                             int *height)
{
  MrgList *l;

  if (!path)
    return NULL;
  for (l = image_cache; l; l = l->next)
  {
    CacheImage *image = l->data;
    if (!strcmp (image->path, path))
    {
      if (width)
        *width = image->width;
      if (height)
        *height = image->height;
      return image->data;
    }
  }
  trim_cache ();
  {
    int w, h, stride;
    unsigned char *data;
    data = image_load (path, &w, &h, &stride);
    if (data)
    {
      CacheImage *image = malloc (sizeof (CacheImage));
      image->data = data;
      image->width = w;
      image->height = h;
      image->path = strdup (path);
      mrg_list_prepend_full (&image_cache, image,
                           (void*)free_image, NULL);
      image_cache_size +=
          compute_size (w, h);

      return cached_image (path, width, height);
    }
  }
  return NULL;
}



