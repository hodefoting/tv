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
#include "tfb.h"




static inline long coldiff(uint32_t col1, uint32_t col2)
{
  uint8_t *c1 = (void*)&col1;
  uint8_t *c2 = (void*)&col2;

  return ((c1[0]-c2[0])* (c1[0]-c2[0]) +
          (c1[1]-c2[1])* (c1[1]-c2[1]) +
          (c1[2]-c2[2])* (c1[2]-c2[2]));
}


typedef struct UnicodeGlyph
{
  const char *utf8;
  uint16_t    bitmap;
} UnicodeGlyph;

UnicodeGlyph glyphs[]={{
        
" ", 0b\
0000\
0000\
0000\
0000},{


"▘", 0b\
1100\
1100\
0000\
0000},{

"▝", 0b\
0011\
0011\
0000\
0000},{

"▀", 0b\
1111\
1111\
0000\
0000},{
        
"▖", 0b\
1111\
1111\
0000\
0000},{

"▌", 0b\
1100\
1100\
1100\
1100},{
        
"▞", 0b\
0011\
0011\
1100\
1100},{
        
"▛", 0b\
1111\
1111\
1100\
1100  },{
        
"▗", 0b\
0000\
0000\
0011\
0011},{
        
"▚", 0b\
1100\
1100\
0011\
0011},{
        
"▐", 0b\
0011\
0011\
0011\
0011},{
        
"▜", 0b\
1111\
1111\
0011\
0011},{
        
"▄", 0b\
0000\
0000\
1111\
1111},{
        
"▙", 0b\
1100\
1100\
1111\
1111},{
        
"▟", 0b\
0011\
0011\
1111\
1111},{
        
"█", 0b\
1111\
1111\
1111\
1111},{

"⬩", 0b\
0000\
0100\
0000\
0000},{

"▪", 0b\
0000\
0110\
0110\
0000},{


"▎", 0b\
1000\
1000\
1000\
1000},{

"┃", 0b\
0110\
0110\
0110\
0110},{



"╹", 0b\
0110\
0110\
0000\
0000},{

"╻", 0b\
0000\
0000\
0110\
0110},{

"╸", 0b\
0000\
1100\
1100\
0000},{

"╺", 0b\
0000\
0011\
0011\
0000},{

"▕", 0b\
0001\
0001\
0001\
0001},{

"▂", 0b\
0000\
0000\
0000\
1111},{

"▆", 0b\
0000\
1111\
1111\
1111},{


"▊", 0b\
1110\
1110\
1110\
1110},{

#if 1

"▪", 0b\
0000\
0010\
0110\
0000},{


"▪", 0b\
0000\
0110\
0100\
0000},{


"▪", 0b\
0000\
0100\
0110\
0000},{

"▪", 0b\
0000\
0110\
0010\
0000},{

"◆", 0b\
0100\
1110\
0111\
0010},{
 
"╱", 0b\
0001\
0010\
0100\
1000},{

"╳", 0b\
1001\
0110\
0110\
1001},{

"╲", 0b\
1000\
0100\
0010\
0001},{

"─", 0b\
0000\
0000\
1111\
0000},{
#endif


"T", 0b\
0000\
1110\
0100\
0100},{

"L", 0b\
0000\
0100\
0110\
0000},{

"\"", 0b\
1010\
1010\
0000\
0000},{

"`", 0b\
1000\
0100\
0000\
0000},{

".", 0b\
0000\
0000\
0010\
0000},{


"┃", 0b\
0110\
0110\
0110\
0110},{

"━", 0b\
0000\
1111\
1111\
0000},{

"╋", 0b\
0110\
1111\
1111\
0110},{

"+", 0b\
0100\
1110\
0100\
0000},{

"=", 0b\
1110\
0000\
1110\
0000},{

"-", 0b\
0000\
1110\
0000\
0000},{

"▒", 0b\
0101\
1010\
0101\
1010},{


NULL, 0}
};


void sixel_out_char (int ch)
{
  printf( "%c", ch);
}

void sixel_out_str (const char *str)
{
  int i;
  for (i = 0; str[i]; i++)
    sixel_out_char (str[i]);
}

#define sixel_outf(fmt, args...) do \
{ \
  char tmp[256]; \
  snprintf (tmp, 256, fmt, ##args); \
  sixel_out_str (tmp);\
} while(0)

int current = -1;
int count = 0;

void sixel_flush (void)
{
  if (count == 0)
  {
    current = -1;
    return;
  }
  while (count > 255) /* vt240 repeat count limit */
  {
    fprintf( stdout,"!%d%c", 255, current + '?');
    count -= 255;
  }
  switch (count)
  {
    case 3: sixel_out_char (current + '?');
    case 2: sixel_out_char (current + '?');
    case 1: sixel_out_char (current + '?');
    break;
    default:
      printf( "!%d%c", count, current + '?'); // XXX: port to out..
    break;
  }
  current = -1;
  count = 0;
}

void sixel_out (int sixel)
{
  if (current == sixel)
    count ++;
  else
  {
    if (current != -1)
      sixel_flush ();
    current = sixel;
    count = 1;
  }
}

void sixel_nl ()
{
  sixel_flush ();
  sixel_out_char ('-');
}

void sixel_cr ()
{
  sixel_flush ();
  sixel_out_char ('$');
}

void sixel_end ()
{
  sixel_out_str ( "\\");
}

void sixel_start ()
{
  //sixel_out_str ("[?80h");
  sixel_out_str ( "P9;0;q");
}


void term_home ()
{
  //sixel_out_str ( "c");
  sixel_out_str ( "[1H");
}


static inline float mask_a (int x, int y, int c)
{
  return ((((x + c * 67) + y * 236) * 119) & 255 ) / 128.0 / 2;
}

static inline float mask_x (int x, int y, int c)
{
  return ((((x + c * 67) ^ y * 236) * 119) & 255 ) / 128.0 / 2;
}

static float (*mask)(int x, int y, int c) = mask_a;

typedef struct PalInfo {
  int start_pal;
  int end_pal;
  int red_levels;
  int green_levels;
  int blue_levels;
} PalInfo;

PalInfo infos[]={
 {8, 11, 2, 2, 2},
 {12, 15, 2, 3, 2},
 {16, 22, 2, 4, 2},
 {24, 31, 3, 4, 2},
 {32, 63, 3, 3, 3},
 {64, 124, 4, 4, 4},
 {125, 149, 5, 5, 5},
 {150, 215, 5, 6, 5},
 {216, 239, 6, 6, 6},
 {240, 251, 6, 8, 5},  // should use this one for 8bpp fb - and leave 16 first c# alone
 {252, 255, 6, 7, 6},
 {256, 342, 8, 8, 4},
 {343, 411, 7, 7, 7},
 {512, 728, 8, 8, 8},
 {729, 999, 9, 9, 9},
 {1000, 1330, 10, 10, 10},
 {1331, 1727, 11, 11, 11},
 {1728, 2196, 12, 12, 12},
 {2197, 2743, 13, 13, 13},
 {2744, 3374, 14, 14, 14},
 {3375, 4095, 15, 15, 15},
 {4096, 10240, 16, 16, 16},
};

void palcount_to_levels (int palcount,
                         int *red_levels,
                         int *green_levels,
                         int *blue_levels,
                         int *grayscale)
{
  int i;
  for (i = 0; i < sizeof(infos)/sizeof(infos[0]); i++)
    if (infos[i].start_pal <= palcount && palcount <= infos[i].end_pal )
      {
        *red_levels = infos[i].red_levels;
        *green_levels = infos[i].green_levels;
        *blue_levels = infos[i].blue_levels;
        return;
      }
}

static inline void memcpy32_16 (uint8_t *dst, const uint8_t *src, int count,
                                int y, int x)
{
  int red_levels = (1 << 5);
  int green_levels = (1 << 6);
  int blue_levels = (1 << 5);
  while (count--)
    {
      int dithered[3];
      dithered[0] = src[0] + mask (x, y, 0) * 255/(red_levels-1);
      dithered[1] = src[1] + mask (x, y, 1) * 255/(green_levels-1);
      dithered[2] = src[2] + mask (x, y, 2) * 255/(blue_levels-1);
      for (int c = 0; c < 3; c++)
      {
        dithered[c] = dithered[c] > 255 ? 255 : dithered[c];
        dithered[c] = dithered[c] < 0 ? 0 : dithered[c];
      }
      int big = ((dithered[2] * (blue_levels-1) / 255)) +
                ((dithered[1] * (green_levels-1) / 255) << 5) +
                ((dithered[0] * (red_levels-1) / 255) << 11);
      dst[1] = big >> 8;
      dst[0] = big & 255;
      dst+=2;
      src+=4;
      x++;
    }
}

static inline void memcpy32_15 (uint8_t *dst, const uint8_t *src, int count,
                                int y, int x)
{
  int red_levels = (1 << 5);
  int green_levels = (1 << 5);
  int blue_levels = (1 << 5);
  while (count--)
    {
      int dithered[3];
      dithered[0] = src[0] + mask (x, y, 0) * 255/(red_levels-1);
      dithered[1] = src[1] + mask (x, y, 1) * 255/(green_levels-1);
      dithered[2] = src[2] + mask (x, y, 2) * 255/(blue_levels-1);
      for (int c = 0; c < 3; c++)
      {
        dithered[c] = dithered[c] > 255 ? 255 : dithered[c];
        dithered[c] = dithered[c] < 0 ? 0 : dithered[c];
      }
      int big = ((dithered[2] * (blue_levels-1)  / 255)) +
                ((dithered[1] * (green_levels-1) / 255) << 5) +
                ((dithered[0] * (red_levels-1)   / 255) << 10);
      dst[1] = big >> 8;
      dst[0] = big & 255;
      dst+=2;
      src+=4;
      x++;
    }
}

static inline void memcpy32_8 (uint8_t *dst, const uint8_t *src, int count,
                               int y, int x)
{
  int red_levels = 6;
  int green_levels = 7;
  int blue_levels = 6;
  while (count--)
    {
      int dithered[3];
      dithered[0] = src[0] + mask (x, y, 0) * 255/(red_levels-1);
      dithered[1] = src[1] + mask (x, y, 1) * 255/(green_levels-1);
      dithered[2] = src[2] + mask (x, y, 2) * 255/(blue_levels-1);
      for (int c = 0; c < 3; c++)
      {
        dithered[c] = dithered[c] > 255 ? 255 : dithered[c];
        dithered[c] = dithered[c] < 0 ? 0 : dithered[c];
      }
      int big = ((dithered[2] * (blue_levels-1) / 255)) +
                ((dithered[1] * (green_levels-1) / 255) * 6) +
                ((dithered[0] * (red_levels-1) / 255) * 6 * 7);
      dst[0] = big & 0xff;
      dst+=1;
      src+=4;
      x++;
    }
}

static inline void memcpy32_24 (uint8_t *dst, const uint8_t *src, int count)
{
  while (count--)
    {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst+=3;
      src+=4;
    }
}

void blit_sixel_pal (unsigned int        *pal,
                     int                  rowstride,
                     int                  x0,
                     int                  y0,
                     int                  outw,
                     int                  outh,
                     int                  grayscale,
                     int                  palcount,
                     int                  transparency
                 )
{
  int red, green, blue;
  int red_max, green_max, blue_max;
  int red_levels   = 2;
  int green_levels = 4;
  int blue_levels  = 2;

  palcount_to_levels (palcount, &red_levels, &green_levels, &blue_levels, &grayscale);
  red_max = red_levels;
  green_max = green_levels;
  blue_max = blue_levels;

  if (grayscale)
    {
      red_max = 1;
      blue_max = 1;
      red_levels = green_levels = blue_levels = green_max = palcount;
    }
  int x, y;

  term_home ();
  sixel_start ();

  int palno = 0;
  for (red   = 0; red   < red_max; red++)
  for (green = 0; green < green_max; green++)
  for (blue  = 0; blue  < blue_max; blue++)
  {
    if (grayscale)
      sixel_outf ( "#%d;2;%d;%d;%d", palno, green * 100/(green_levels-1),
                                     green * 100/(green_levels-1),
                                     green * 100/(green_levels-1));
    else  
      sixel_outf ( "#%d;2;%d;%d;%d", palno, red * 100/(red_levels-1),
                                            green * 100/(green_levels-1),
                                            blue * 100/(blue_levels-1));
    palno++;
  }

  for (y = 0; y < outh; )
  {
    palno=0;
    for (red   = 0; red   < red_max;   red++)
    for (green = 0; green < green_max; green++)
    for (blue  = 0; blue  < blue_max;  blue++)
    {
      int setpal = 0;

      for (x = 0; x < outw; x ++)
      {
        int sixel = 0;
        int v;
        for (v = 0; v < 6; v++) // XXX: the code redithers,
                                //      instead of dithering to
                                //      a tempbuf and then blitting that
                                //      buf to sixel
          {
            int got_coverage = 0;
            int offset = ((y + v) * outw + x) * 4;
            got_coverage = pal[offset+3] >= 0;

            if (got_coverage)
              if (pal[offset/4] == palno)
                sixel |= (1<<v);
          }

          if (sixel && !setpal)
            {
               sixel_outf ( "#%d", palno);
               setpal = 1;
            }
          sixel_out (sixel);
       }

#ifdef SKIP_FULL_BLANK_ROWS
      /* skip outputting entirely transparent, could even skip the set color and carriage return */
       if (count == outw &&
           current == 0)
       {
         count = 0;
         current = -1;
       }
       else
       {
         sixel_cr ();
       }
#else
       sixel_cr ();
#endif
       palno++;
     }
     sixel_nl ();
     y += 6;

     if (stdin_got_data(1))
     {
       sixel_end ();
       return;
     }
  }
  sixel_end ();
}


void dither_rgba (Tfb *tfb,
                const unsigned char *rgba,
                  unsigned int         *pal,
                  int                  rowstride,
                  int                  outw,
                  int                  outh,
                  int                  grayscale,
                  int                  palcount,
                  int                  transparency
                 )
{
  int red_levels   = 2;
  int green_levels = 4;
  int blue_levels  = 2;
  palcount_to_levels (palcount, &red_levels, &green_levels, &blue_levels, &grayscale);
  if (grayscale)
    {
      red_levels = green_levels = blue_levels = palcount;
    }
  int x, y;
  for (y = 0; y < outh; )
  {
    {
      for (x = 0; x < outw; x ++)
      {
        int dithered[4];
        int got_coverage = 0;
        int offset = (y) * outw * 4 + x*4;
        got_coverage = rgba[offset+3] > 127;

        if (got_coverage)
          {
            dithered[0] = rgba[offset+0];
            dithered[1] = rgba[offset+1];
            dithered[2] = rgba[offset+2];
            dithered[3] = rgba[offset+3];
            if (tfb->do_dither)
            {
              dithered[0] += mask (x, y, 0) * 255/(red_levels-1);
              dithered[1] += mask (x, y, 1) * 255/(green_levels-1);
              dithered[2] += mask (x, y, 2) * 255/(blue_levels-1);
            }
            else
            {
              dithered[0] += 0.5 * 255/(red_levels-1);
              dithered[1] += 0.5 * 255/(green_levels-1);
              dithered[2] += 0.5 * 255/(blue_levels-1);
            }

            if (grayscale)
            {
              dithered[1] = (dithered[0] + dithered[1] + dithered[2])/3;
              pal[offset/4] = (dithered[1] * (green_levels -1) / 255);
            }
            else
            {
              if (dithered[0] > 255)
                dithered[0] = 255;
              if (dithered[1] > 255)
                dithered[1] = 255;
              if (dithered[2] > 255)
                dithered[2] = 255;

              if (dithered[0] < 0)
                dithered[0] = 0;
              if (dithered[1] < 0)
                dithered[1] = 0;
              if (dithered[2] < 0)
                dithered[2] = 0;

             pal[offset/4] = 0 + 
                (dithered[0] * (red_levels-1)   / 255) * blue_levels * green_levels+
                (dithered[1] * (green_levels-1)  / 255) * blue_levels + 
                (dithered[2] * (blue_levels-1)  / 255);
            }
          }
         else 
          {
             pal[offset/4] = 0;
          }
       }
     }
     y ++;
  }
}

void set_fg(int red, int green, int blue)
{
  sixel_outf("[48;2;%i;%i;%im", red,green,blue);
}
void set_bg(int red, int green, int blue)
{
  sixel_outf("[38;2;%i;%i;%im", red,green,blue);
}

void paint_rgba (Tfb *tfb, uint8_t *rgba, int outw, int outh)
{
  switch (tfb->tv_mode)
  {
    case TV_ASCII:
      {
        unsigned int *pal = calloc (outw * 4 * outh * sizeof (int), 1);
        dither_rgba (tfb, rgba,
          pal,
          outw * 4,
          outw,
          outh,
          1,
          2,
          0);
        if (tfb->interactive == 0 || !stdin_got_data (1))
        {
          int x, y;
          for (y = 0; y < outh-2; y+=2)
          {
            for (x = 0; x < outw; x+=2)
            {
              static char *ascii_quarts[]={" ","`","'","\"",",","[","/","P",".","\\","]","?","o","b","d","8",NULL};
              int bitmask = 0;
              int o = y * outw + x;
              if (pal[o]!=0)        bitmask |= (1<<0); //1
              if (pal[o+1]!=0)      bitmask |= (1<<1); //2
              if (pal[o+outw]!=0)   bitmask |= (1<<2); //4
              if (pal[o+outw+1]!=0) bitmask |= (1<<3); //8
              sixel_outf (ascii_quarts[bitmask]);
            }
            sixel_outf ("\n");
          }
        }
        free (pal);
      }
      break;

    case TV_UTF8:
      {
        if (tfb->interactive == 0 || !stdin_got_data (1))
        {
          if (tfb->interactive)
            term_home ();
          /* quantization used for approximate matches */
          uint32_t mask = 0xf0f0f0f0;

          for (int y = 0; y < outh-4; y+=4)
          {
            for (int x = 0; x < outw - 4; x+=4)
            {
              int best_glyph = 0;
              int best_matches = 0;
              int best_is_inverted = 0;
              int rgbo = y * outw * 4 + x * 4;

              uint32_t maxc = 0;
              uint32_t secondmaxc = 0;
              int counts[16]={0,0,0,0};
              uint32_t colors[16]={0,0,0,0};
              int max = 0;
              int secondmax = 0;
              int c = 0;
              for (int u = 0; u < 4; u++)
              for (int v = 0; v < 4; v++)
                {
                  int found = 0;
                  for (int i = 0; i < c && found==0; i++)
                  {
                    if ((colors[i] & mask) == 
                        (*((uint32_t*)(&rgba[rgbo+outw*4*v+u*4])) & mask))
                    {
                      counts[i]++;
                      found = 1;
                    }
                  }
                  if (!found)
                  {
                    colors[c] = *((uint32_t*)(&rgba[rgbo+outw*4*v+u*4]));
                    counts[c] = 1;
                    c++;
                  }
                }
              for (int i = 0; i < c; i++)
              {
                if (counts[i]>=max)
                {
                  secondmaxc = maxc;
                  secondmax = max;
                  max = counts[i];
                  maxc = colors[i];
                }
              }
              for (int i = 0; i < c; i++)
              {
                if (counts[i]>=secondmax && colors[i] != maxc)
                {
                  secondmax = counts[i];
                  secondmaxc = colors[i];
                }
              }

              for (int i = 0; glyphs[i].utf8; i++)
              {
                int matches = 0;
                int bitno = 0;
                for (int v = 3; v >=0; v --)
                  for (int u = 3; u >=0; u --)
                  {
                    uint32_t col = *((uint32_t*)(&rgba[rgbo + outw * 4 * v + u * 4]));
                    long d1 = coldiff(col, maxc);
                    long d2 = coldiff(col, secondmaxc);
                    int col1 = 0;
                    int col2 = 0;

                    if (d1 < d2) col1 = 1;
                    else col2 = 1;

                    if (col1)
                    {
                      if (glyphs[i].bitmap & (1<<bitno))
                        matches ++;
                    } else if (col2)
                    {
                      if ((glyphs[i].bitmap & (1<<bitno)) == 0)
                        matches ++;
                    }
                    bitno++;
                  }
                if (matches > best_matches)
                {
                  best_matches = matches;
                  best_glyph = i;
                  best_is_inverted = 0;
                }
              }


              /* do second run in inverse video  */
              for (int i = 20; glyphs[i].utf8; i++)
              {
                int matches = 0;
                int bitno = 0;
                for (int v = 3; v >=0; v --)
                  for (int u = 3; u >=0; u --)
                  {
                    uint32_t col = *((uint32_t*)(&rgba[rgbo + outw * 4 * v + u * 4]));
                    long d1 = coldiff(col, maxc);
                    long d2 = coldiff(col, secondmaxc);
                    int col1 = 0;
                    int col2 = 0;

                    if (d1 < d2) col1 = 1;
                    else col2 = 1;

                    if (col1)
                    {
                      if ((!glyphs[i].bitmap) & (1<<bitno))
                        matches ++;
                    } else if (col2)
                    {
                      if (((!glyphs[i].bitmap) & (1<<bitno)) == 0)
                        matches ++;
                    }
                    bitno++;
                  }
                if (matches >= best_matches)
                {
                  best_matches = matches;
                  best_glyph = i;
                  best_is_inverted = 1;
                }
              }

              /* XXX: re-calibrate color to actual best colors for glyph*/
              if(1){
                long red0 = 0, green0 = 0, blue0 = 0;
                long red1 = 0, green1 = 0, blue1 = 0;
                int bitno = 0;
                int count0 = 0, count1 = 0;
                for (int v = 3; v >=0; v --)
                  for (int u = 3; u >=0; u --)
                  {
                    uint32_t col = *((uint32_t*)(&rgba[rgbo + outw * 4 * v + u * 4]));

                    if (((glyphs[best_glyph].bitmap) & (1<<bitno)))
                    {
                      red0 += col & 0xff;
                      green0 += (col >> 8) & 0xff;
                      blue0 += (col >> 16) & 0xff;
                      count0++;
                    }
                    else
                    {
                      red1 += col & 0xff;
                      green1 += (col >> 8) & 0xff;
                      blue1 += (col >> 16) & 0xff;
                      count1++;
                    }
                    bitno++;
                  }
                if (count0)
                {
                red0/=count0;
                green0/=count0;
                blue0/=count0;
                }
                if (count1)
                {
                red1/=count1;
                green1/=count1;
                blue1/=count1;
                }
                if (best_is_inverted)
                {
                  secondmaxc = red0 + (green0 << 8) + (blue0 << 16);
                  maxc = red1 + (green1 << 8) + (blue1 << 16);
                }
                else
                {
                  maxc = red0 + (green0 << 8) + (blue0 << 16);
                  secondmaxc = red1 + (green1 << 8) + (blue1 << 16);
                }
              }

              if (best_is_inverted)
              {
                set_fg ((maxc)&0xff,(maxc >> 8)&0xff  , (maxc >> 16) & 0xff);
                set_bg ((secondmaxc)&0xff,(secondmaxc >> 8)&0xff  , (secondmaxc >> 16) & 0xff);
              }
              else
              {
                set_bg ((maxc)&0xff,(maxc >> 8)&0xff  , (maxc >> 16) & 0xff);
                set_fg ((secondmaxc)&0xff,(secondmaxc >> 8)&0xff  , (secondmaxc >> 16) & 0xff);
              }

              sixel_outf(glyphs[best_glyph].utf8);
            }
          sixel_outf ("\n");
          sixel_outf("[0m");
        }
      }
      }
      break;
    case TV_FB:
      {
        int scan;
        int fb_fd = open ("/dev/fb0", O_RDWR);
        unsigned char *fb = mmap (NULL, tfb->fb_mapped_size, PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);

        for (scan = 0; scan < outh; scan++)
        {
          unsigned char *src = rgba + outw * 4 * scan;
          unsigned char *dst = fb + tfb->fb_stride * scan;
          int x;

	      switch (tfb->fb_bpp)
            {
              case 32:
              for (x= 0; x < outw; x++)
              {
                dst[0]=src[2];
                dst[1]=src[1];
                dst[2]=src[0];
                dst[3]=src[3];
                src+=4;
                dst+=4;
              }
              break;
              case 24:
                memcpy32_24 (dst, src, outw);
                break;
              case 16:
                memcpy32_16 (dst, src, outw, scan, 0);
                break;
              case 15:
                memcpy32_15 (dst, src, outw, scan, 0);
                break;
              case 8:
                memcpy32_8 (dst, src, outw, scan, 0);
                break;
            }
        }
        munmap (fb, tfb->fb_mapped_size);
        fflush (stdout);
        close (fb_fd);
      }
      break;
    case TV_SIXEL_HI:
    case TV_SIXEL:
      {
  unsigned int *pal = calloc (outw * 4 * outh * sizeof (int), 1);
  dither_rgba (tfb, rgba, pal, outw * 4, outw, outh, tfb->grayscale, tfb->palcount, 0);
  if (!stdin_got_data (1))
  blit_sixel_pal (pal, outw * 4, 0, 0, outw, outh, tfb->grayscale, tfb->palcount, 0
              );
  free (pal);
       }
       break;
    case TV_AUTO:
      fprintf (stderr, "uh? %i", __LINE__);
      break;
  }
}

static void term_get_xy (int* x, int *y)
{
    struct termios term, orig_term;
    tcgetattr (STDIN_FILENO, &orig_term);
    term = orig_term;
    term.c_lflag &=~ICANON;
    term.c_lflag &=~ECHO;
    tcsetattr (STDIN_FILENO, TCSANOW, &term);

    printf ("[6n");
    fflush(stdout);

    if (stdin_got_data (100000))
      if (scanf("\033[%d;%dR", y, x) != 2)
      {
        *x = 1;
        *y = 1;
      }
    tcsetattr (STDIN_FILENO, TCSADRAIN, &orig_term);
}


int sixel_is_supported (void)
{
  static int inited = -22;
  /* check if issuing sixel commands that would move the cursor has such an
     effect  */
  int ox, oy;
  int x, y;
  int xb, yb;
  if (inited == -22)
  {
   term_get_xy (&ox, &oy);
   printf ("[1;1H");
   printf ("\r");
   fflush(NULL);
   term_get_xy (&x, &y);
   sixel_start ();
   sixel_outf ("#1---ab7878-----A-");
   sixel_end ();
   printf ("\r");
   fflush(NULL);
   term_get_xy (&xb, &yb);
   printf ( "[%d;%dH", oy, ox);
   fflush(NULL);
   inited = (y != yb);
  }
  return inited;
}



