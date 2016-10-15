#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

#if 0
int sixel_out (int sixel)
{
  sixel_out_char (sixel + '?');
}

void sixel_flush (void)
{
}
#else
int current = -1;
int count = 0;

void sixel_flush (void)
{
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
int sixel_out (int sixel)
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
#endif

int sixel_nl ()
{
  sixel_flush ();
  sixel_out_char ('-');
}

int sixel_cr ()
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
  sixel_out_str ( "P9;0;q");
}

void term_home ()
{
  sixel_out_str ( "[1;1H");
}

static float mask_a (int x, int y, int c)
{
  return ((((x + c * 67) + y * 236) * 119) & 255 ) / 128.0 / 2;
}

char *images[4096]={0,};
int images_c = 0;

#include <sys/ioctl.h>

void init (int *dw, int *dh)
{
    struct winsize size = {0,0,0,0};
    int result = ioctl (0, TIOCGWINSZ, &size);
    if (result) {
      *dw = 400; /* default dims - works for xterm which is small*/
      *dh = 300;
      return;
    }
    *dw = size.ws_xpixel ;
    *dh = size.ws_ypixel - 30;
}

void usage ()
{
  printf ("usage: xv [--help] [-w <width>] [-h <height>] [-o] [-v] [-g] [-p <palcount>] [-nd] image1 image2 image3 image4\n");
  printf ("options:\n");
  printf ("  --help  print this help\n");
  printf ("  -w <int>width in pixels (default terminal width)\n");
  printf ("  -h <int>height in pixels (default terminal iheight)\n");
  printf ("  -o      reset cursor to 0,0 after each drawing\n");
  printf ("  -v      be verbose\n");
  printf ("  -g      do a grayscale instead of color\n");
  printf ("  -nd     no dithering\n");
  printf ("  -p <count>  use count number of colors\n");
  exit (0);
}

static int nc_is_raw = 0;
static int atexit_registered = 0;
static struct termios orig_attr; 

static void _nc_noraw (void)
{
  orig_attr.c_lflag |= (ECHO | ICANON);
  if (nc_is_raw && tcsetattr (STDIN_FILENO, TCSAFLUSH, &orig_attr) != -1)
    nc_is_raw = 0;
}


static void
sixel_at_exit (void)
{
  _nc_noraw();
}

static int _nc_raw (void)
{
  struct termios raw;
  if (!isatty (STDIN_FILENO))
    return -1;
  if (!atexit_registered)
    {
      atexit (sixel_at_exit);
      atexit_registered = 1;
    }
  if (tcgetattr (STDIN_FILENO, &orig_attr) == -1)
    return -1;
  raw = orig_attr;  /* modify the original mode */
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */
  if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &raw) < 0)
    return -1;
  nc_is_raw = 1;
  tcdrain(STDIN_FILENO);
  tcflush(STDIN_FILENO, 1);
  return 0;
}

float factor   = -1.0;
float x_offset = 0.0;
float y_offset = 0.0;
  int palcount = 16;
  int do_dither = 1;
  int grayscale = 0;
  float delay = 1.0;
  int verbosity = 0;
  int desired_width =  1024;
  int desired_height = 1024;
  int image_no;
  unsigned char *image = NULL;
  int image_w, image_h;

typedef enum {
  RENONE=0,
  RELOAD,
  REDRAW,
  REQUIT,
  REEVENT,
  REIDLE,
} EvReaction;


typedef struct Action {
  const char *input;
  EvReaction (*handler) (void);
} Action;

#define JUMPLEN 0.50

EvReaction cmd_up (void)
{
  y_offset = y_offset - (desired_height * JUMPLEN) * factor;
  if (y_offset < 0)
    y_offset = 0;
  return REDRAW;
}

EvReaction cmd_down (void)
{
  y_offset = y_offset + (desired_height * JUMPLEN) * factor;
  return REDRAW;
}

EvReaction cmd_right (void)
{
  x_offset = x_offset + (desired_height * JUMPLEN) * factor;
  return REDRAW;
}

EvReaction cmd_left (void)
{
  x_offset = x_offset - (desired_height* JUMPLEN) * factor;
  if (x_offset < 0)
    x_offset = 0;
  return REDRAW;
}

EvReaction cmd_zoom_in (void)
{
  x_offset *= 1.5;
  y_offset *= 1.5;
  factor /= 1.5;
  return REDRAW;
}

EvReaction cmd_zoom_out (void)
{
  x_offset /= 1.5;
  y_offset /= 1.5;
  factor *= 1.5;
  return REDRAW;
}

EvReaction cmd_zoom_fit (void)
{
  x_offset = 0;
  y_offset = 0;

  factor = 1.0 * image_w / desired_width;
  if (factor < 1.0 * image_h / desired_height)
    factor = 1.0 * image_h / desired_height;
  return REDRAW;
}

EvReaction cmd_zoom_1 (void)
{
  x_offset = 0;
  y_offset = 0;

  factor = 1.0;
  return REDRAW;
}



EvReaction cmd_quit (void)
{
  return REQUIT;
}

EvReaction cmd_next (void)
{
  image_no ++;
  if (image_no >= images_c)
    image_no = images_c - 1;
  factor = -1;
  return RELOAD;
}

EvReaction cmd_prev (void)
{
  image_no --;
  if (image_no < 0)
    image_no = 0;
  factor = -1;
  return RELOAD;
}

Action actions[] = {
  {"[A", cmd_up},
  {"[B", cmd_down},
  {"[C", cmd_right},
  {"[D", cmd_left},
  {" ",    cmd_next},
  {"n",    cmd_next},
  {"",   cmd_prev},
  {"p",    cmd_prev},
  {"f",    cmd_zoom_fit},
  {"1",    cmd_zoom_1},
  {"+",    cmd_zoom_in},
  {"=",    cmd_zoom_in},
  {"-",    cmd_zoom_out},
  {"q",    cmd_quit},
  {NULL, NULL}
};


EvReaction handle_input (void)
{
   struct timeval tv;
   int retval;
   fflush (NULL);
   fd_set rfds;
   FD_ZERO (&rfds);
   FD_SET (STDIN_FILENO, &rfds);
   tv.tv_sec = 0; tv.tv_usec = 0;
   retval = select (1, &rfds, NULL, NULL, &tv);
   if (retval == 1)
   {
     char buf[10];
     int length = 0;
     if ((length=read (STDIN_FILENO, &buf[0], 5)) >= 0)
     {
       buf[length]='\0';
       for (int i = 0; actions[i].input; i++)
         if (!strcmp (actions[i].input, buf))
           return actions[i].handler();

       if (0)
       {
       for (int i = 0; i < length; i++)
         fprintf (stderr, " [%d]=%c (%i)   \n     \n", i, buf[i]>32?buf[i]:' ', buf[i]);
       fprintf (stderr, "len: %i [0]=(%c)%i   \n", length, buf[0]>32?buf[0]:' ', buf[0]);
       }

       return REEVENT;
     }
  }
  return REIDLE;
}

int main (int argc, char **argv)
{
  int x, y;
  int red;
  int green;
  int blue;
  int red_max, green_max, blue_max;
  int zero_origin = 0;
  const char *path = NULL;

  int interactive = 0;


  /* we initialize the terminals dimensions as defaults, before the commandline
     gets to override these dimensions further 
   */
  init (&desired_width, &desired_height);

  for (x = 1; argv[x]; x++)
  {
    if (!strcmp (argv[x], "--help"))
    {
      usage ();
    }
    else if (!strcmp (argv[x], "-v"))
    {
      verbosity ++;
    }
    else if (!strcmp (argv[x], "-g"))
    {
      grayscale = 1;
    }
    else if (!strcmp (argv[x], "-o"))
    {
      zero_origin  = 1;
    }
    else if (!strcmp (argv[x], "-nd"))
    {
      do_dither = 0;
    }
    else if (!strcmp (argv[x], "-s"))
    {
      if (!argv[x+1])
        return -2;
      factor = 1.0 / strtod (argv[x+1], NULL);
      x++;
    }
    else if (!strcmp (argv[x], "-x"))
    {
      if (!argv[x+1])
        return -2;
      x_offset = strtod (argv[x+1], NULL);
      x++;
    }
    else if (!strcmp (argv[x], "-y"))
    {
      if (!argv[x+1])
        return -2;
      y_offset = strtod (argv[x+1], NULL);
      x++;
    }
    else if (!strcmp (argv[x], "-p"))
    {
      if (!argv[x+1])
        return -2;
      palcount = atoi (argv[x+1]);
      x++;
    }
    else if (!strcmp (argv[x], "-w"))
    {
      if (!argv[x+1])
        return -2;
      desired_width = atoi (argv[x+1]);
      x++;
    }
    else if (!strcmp (argv[x], "-d"))
    {
      if (!argv[x+1])
        return -2;
      delay = strtod (argv[x+1], NULL);
      x++;
    }
    else if (!strcmp (argv[x], "-i"))
    {
      interactive = 1;
    }
    else if (!strcmp (argv[x], "-h"))
    {
      if (!argv[x+1])
        return -2;
      desired_height = atoi (argv[x+1]);
      x++;
    }
    else
    {
      images[images_c++] = argv[x];
    }
  }
  images[images_c] = NULL;


  for (image_no = 0; image_no < images_c; image_no++)
  {
interactive_load_image:


    path = images[image_no];

  image = stbi_load (path, &image_w, &image_h, NULL, 4);

  if (factor < 0)
  {
    cmd_zoom_fit ();
  }

  if (!image)
  {
    return -1;
  }
  int   RED_LEVELS  = 2;
  int   GREEN_LEVELS = 4;
  int   BLUE_LEVELS  = 2;

  {
    if (palcount >= 252)
    {
        RED_LEVELS   = 6;
        GREEN_LEVELS = 7;
        BLUE_LEVELS  = 6;
    }
    else if (palcount >= 216)
    {
        RED_LEVELS   = 6;
        GREEN_LEVELS = 6;
        BLUE_LEVELS  = 6;
    }
    else if (palcount >= 180)
    {
        RED_LEVELS   = 6;
        GREEN_LEVELS = 6;
        BLUE_LEVELS  = 5;
    }
    else if (palcount >= 150)
    {
        RED_LEVELS   = 5;
        GREEN_LEVELS = 6;
        BLUE_LEVELS  = 5;
    }
    else if (palcount >= 125)
    {
        RED_LEVELS   = 5;
        GREEN_LEVELS = 5;
        BLUE_LEVELS  = 5;
    }
    else if (palcount >= 100)
    {
        RED_LEVELS   = 5;
        GREEN_LEVELS = 5;
        BLUE_LEVELS  = 4;
    }
    else if (palcount >= 80)
    {
        RED_LEVELS   = 4;
        GREEN_LEVELS = 5;
        BLUE_LEVELS  = 4;
    }
    else if (palcount >= 64)
    {
        RED_LEVELS  = 4;
        GREEN_LEVELS = 4;
        BLUE_LEVELS = 4;
    }
    else if (palcount >= 48)
    {
        RED_LEVELS  = 4;
        GREEN_LEVELS = 4;
        BLUE_LEVELS = 3;
    }
    else if (palcount >= 36)
    {
        RED_LEVELS  = 3;
        GREEN_LEVELS = 4;
        BLUE_LEVELS = 3;
    }
    else if (palcount >= 32)
    {
        RED_LEVELS  = 3;
        GREEN_LEVELS = 3;
        BLUE_LEVELS = 3;
    }
    else if (palcount >= 24)
    {
        RED_LEVELS  = 3;
        GREEN_LEVELS = 4;
        BLUE_LEVELS = 2;
    }
    else if (palcount >= 16)
    {
        RED_LEVELS   = 2;
        GREEN_LEVELS = 4;
        BLUE_LEVELS  = 2;
    }
    else 
    {
      grayscale = 1;
    }
  }

  red_max = RED_LEVELS;
  green_max = GREEN_LEVELS;
  blue_max = BLUE_LEVELS;

  if (grayscale)
  {
    red_max = 1;
    blue_max = 1;
    RED_LEVELS = GREEN_LEVELS = BLUE_LEVELS = green_max = palcount;
  }


  // image = rescale_image (image, &w, &h, desired_width, desired_height);
  int outw = desired_width;
  int outh = desired_height;

  if (interactive)
  {
    zero_origin = 1;
    _nc_raw();
  }
  
interactive_again:
    init (&desired_width, &desired_height);
  {
    if (zero_origin)
      term_home ();
    
    sixel_start ();
      int palno = 1;
      for (red   = 0; red   < red_max; red++)
      for (blue  = 0; blue  < blue_max; blue++)
      for (green = 0; green < green_max; green++)
      {
        if (grayscale)
          sixel_outf ( "#%d;2;%d;%d;%d", palno, green * 100/(GREEN_LEVELS-1),
                                           green * 100/(GREEN_LEVELS-1),
                                           green * 100/(GREEN_LEVELS-1));
        else  
          sixel_outf ( "#%d;2;%d;%d;%d", palno, red * 100/(RED_LEVELS-1),
                                         green * 100/(GREEN_LEVELS-1),
                                          blue * 100/(BLUE_LEVELS-1));
        palno++;
      }

      /* do resampling as part of view, not as a separate step */
      for (y = 0; y < outh; y)
      {
        palno=1;
        for (red   = 0; red   < red_max; red++)
        for (blue  = 0; blue  < blue_max; blue++)
        for (green = 0; green < green_max; green++)
        {
          sixel_outf ( "#%d", palno++);
          for (x = 0; x < outw; x ++)
          {
            int binary = 0;
            int v;
            int q = x * factor + x_offset;
            int dithered[4];
            for (v = 0; v < 6; v++)
            {
              int got_coverage = 0;
              int z;

              z = (y + v) * factor + y_offset;
              
              int offset = (int)((z) * image_w + q)*4;

              if (z < image_h &&
                  q < image_w && z >= 0 && q >= 0)
                got_coverage = image[offset+3] > 127;

              if (got_coverage)
              {
                if (do_dither)
                {
                  dithered[0] = image[offset + 0] + mask_a (x, y + v, 0) * 255/(RED_LEVELS-1);
                  dithered[1] = image[offset + 1] + mask_a (x, y + v, 1) * 255/(GREEN_LEVELS-1);
                  dithered[2] = image[offset + 2] + mask_a (x, y + v, 2) * 255/(BLUE_LEVELS-1);
                }
                else
                {
                  dithered[0] = image[offset + 0];
                  dithered[1] = image[offset + 1];
                  dithered[2] = image[offset + 2];
                }
                if (grayscale)
                {
                  dithered[1] = (dithered[0] + dithered[1] + dithered[2])/3;
                  if ((dithered[1] * (GREEN_LEVELS-1) / 255 == green) &&
                      (dithered[1] * (GREEN_LEVELS-1) / 255 == green) &&
                      (dithered[1] * (GREEN_LEVELS-1) / 255 == green)
                      )
                    binary |= (1<<v);
                }
                else
                {
                  if ((dithered[0] * (RED_LEVELS-1)   / 255 == red) &&
                      (dithered[1] * (GREEN_LEVELS-1) / 255 == green) &&
                      (dithered[2] * (BLUE_LEVELS-1)  / 255 == blue)
                      )
                    binary |= (1<<v);
                }
              }
              else
                binary |= (1<<v);

            }
            sixel_out (binary);
          }
#if 0
          if (count == outw)
          {
            count = 0;
            current = -1;
          }
#endif
          sixel_cr ();
        }
        sixel_nl ();
        y += 6;
#if 1
      if (interactive)
      {
        switch (handle_input())
        {
          case REQUIT:  sixel_end();printf ("."); exit(0); break;
          case REDRAW:  sixel_end();goto interactive_again;
          case RELOAD:  sixel_end();goto interactive_load_image;
          case REEVENT: 
          case REIDLE:
            break;
        }
      }
#endif

      }
      sixel_end ();

      if (verbosity)
        sixel_outf ("%s %ix%i\r", path, image_w, image_h);
      else
        sixel_outf ("\r");

      if (interactive)
      {
        ev_again:
        switch (handle_input())
        {
          case REQUIT:  printf ("."); exit(0); break;
          case REDRAW:  goto interactive_again;
          case RELOAD:  goto interactive_load_image;
          case REEVENT: goto ev_again;
          case REIDLE:
            usleep (0.15 * 1000.0 * 1000.0);
            goto ev_again;
            break;
        }
      }

    }
    free (image);
    if (image_no < images_c - 1)
    {
      usleep (delay * 1000.0 * 1000.0);
    }
  }
  return 0;
}


/* not used anymore - but useful to make thumbnails and similar,. */
void *
rescale_image (char *image, int *pw, int *ph, int max_w, int max_h)
{
  char *new_image;
  int w = *pw;
  int h = *ph;
  float factor = 1.0f * max_w / w;
  int x,y;
  int i;
  if (1.0f * max_h / h < factor)
    factor = 1.0f * max_h / h;
  w = ceil (w * factor);
  h = ceil (h * factor);
  new_image = malloc (w * h * 4);

  i = 0;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
    {
      int u = x / factor;
      int v = y / factor;
      new_image[i + 0] = image[(v * *pw + u) * 4 + 0];
      new_image[i + 1] = image[(v * *pw + u) * 4 + 1];
      new_image[i + 2] = image[(v * *pw + u) * 4 + 2];
      new_image[i + 3] = image[(v * *pw + u) * 4 + 3];
      i+=4;
    }
  *pw = w;
  *ph = h;

  free (image);
  return new_image;
}

