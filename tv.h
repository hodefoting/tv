
#define USE_OPEN_MP 1

#ifdef USE_OPEN_MP
#include <omp.h>
#endif

typedef enum {TV_AUTO,
              TV_ASCII,
              TV_SIXEL,
              TV_SIXEL_HI,
              TV_UTF8,
              TV_FB,
} TvOutput;

typedef struct Tfb {
  int      fb_bpp;
  int      fb_mapped_size;
  int      fb_stride;
  int      palcount;
  int      grayscale;
  int      do_dither;
  int      interactive;
  int      term256;
  int      bw;
  TvOutput tv_mode;
} Tfb;

TvOutput init (Tfb *tfb, int *dw, int *dh);
void paint_rgba (Tfb *tfb, uint8_t *rgba, int outw, int outh);

int stdin_got_data (int usec);

