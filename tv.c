#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <libgen.h>
#include <sys/time.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <ftw.h>
#include "tfb.h"

float          factor         = -1.0;
float          x_offset       = 0.0;
float          y_offset       = 0.0;
float          y_offset_thumb = 0.0;
int            grayscale      = 0;
int            slideshow      = 0;
float          delay          = 4.0;
float          time_remaining = 0.0;
int            verbosity      = 1;
int            desired_width  = 1024;
int            desired_height = 1024;
int            image_no;
unsigned char *image       = NULL;
int            image_w, image_h;
const char    *path        = NULL;
int            pdf         = 0;
int            zero_origin = 0;
int            interactive = 1;
int            thumbs = 0;
float          DIVISOR=5.0;

int            brightness = 0;


Tfb tfb = {
1,1,1,-1,0,0,1,0,0,TV_AUTO
};

float aspect = 1.0;
int rotate = 0;

#define SKIP_FULL_BLANK_ROWS 1
#define JUMPLEN 0.50
#define JUMPSMALLLEN 0.05

char *images[40960]={0,};
const char *pdf_path = NULL;
int images_c = 0;

#include <sys/ioctl.h>

int status_y = 25;
int status_x = 1;

int loop = 1;
#if 0
int fb_bpp = 1;
int fb_mapped_size = 1;
int fb_stride = 1;
int palcount = -1;
#endif

int sixel_is_supported (void);


static unsigned short ored[256], ogreen[256], oblue[256];
static struct fb_cmap ocmap = {0, 256, ored, ogreen, oblue, NULL};
int ocmap_used = 0;

void usage ()
{
  printf ("usage: tv [--help] [-w <width>] [-h <height>] [-o] [-v] [-g] [-p <palcount>] [-nd] <image1|pdf> [image2 [image3 [image4 ...]]]\n");
  printf ("options:\n");
  printf ("  --help  print this help\n");
  printf ("  -w <int>width in pixels (default terminal width)\n");
  printf ("  -h <int>height in pixels (default terminal iheight)\n");
  printf ("  -o      reset cursor to 0,0 after each drawing\n");
  printf ("  -m <ascii|utf8|fb|sixel>\n");
  printf ("  -v      be verbose\n");
  //printf ("  -i      interactive interface; use cursors keys, space/backspace etc.\n");
  printf ("  -s      slideshow mode\n");
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
restore_cmap (void)
{
  if (ocmap_used)
  {
    int fb_fd = open ("/dev/fb0", O_RDWR);
    ioctl (fb_fd, FBIOPUTCMAP, &ocmap);
  }
}

static void
sixel_at_exit (void)
{
  _nc_noraw();

  restore_cmap ();
}

static int _nc_raw (void)
{
  struct termios raw;
  //if (!isatty (STDIN_FILENO))
  //  return -1;
  if (!atexit_registered)
    {
      atexit (sixel_at_exit);
      atexit_registered = 1;
    }
  //if (tcgetattr (STDIN_FILENO, &orig_attr) == -1)
  //  return -1;
  tcgetattr (STDIN_FILENO, &orig_attr);
  raw = orig_attr;  /* modify the original mode */
  raw.c_lflag &= ~(ECHO );
  raw.c_lflag &= ~(ICANON );
  raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */
  //if (tcsetattr (STDIN_FILENO, TCSANOW, &raw) < 0)
  //  return -1;
  tcsetattr (STDIN_FILENO, TCSANOW, &raw);
  nc_is_raw = 1;
  //tcdrain(STDIN_FILENO);
  //tcflush(STDIN_FILENO, 1);
  //fflush(NULL);
  return 0;
}


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

char *message = NULL;
int message_ttl = 0;

EvReaction cmd_help (void)
{
  if (message)
  {
    free (message);
    message = NULL;
  }
  //else
  {
    message = strdup ("zoom: 1wf+- pan: cursor keys  next prev: pgdn,space pgup backspace");
    message_ttl = 50;
  }
  return REDRAW;
}

EvReaction cmd_up (void)
{
  if (thumbs)
    y_offset_thumb -= (desired_height * JUMPLEN) ;
  else
    y_offset -= (desired_height * JUMPLEN) * factor;
#if 0
  if (y_offset < 0)
    y_offset = 0;
#endif
  return REDRAW;
}

EvReaction cmd_down (void)
{
  if (thumbs)
    y_offset_thumb += (desired_height * JUMPLEN) ;
  else
    y_offset += (desired_height * JUMPLEN) * factor;
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
#if 0
  if (x_offset < 0)
    x_offset = 0;
#endif
  return REDRAW;
}

EvReaction cmd_up_small (void)
{
  y_offset = y_offset - (desired_height * JUMPSMALLLEN) * factor;
  if (y_offset < 0)
    y_offset = 0;
  return REDRAW;
}

EvReaction cmd_down_small (void)
{
  y_offset = y_offset + (desired_height * JUMPSMALLLEN) * factor;
  return REDRAW;
}

EvReaction cmd_right_small (void)
{
  x_offset = x_offset + (desired_height * JUMPSMALLLEN) * factor;
  return REDRAW;
}


EvReaction cmd_brightness_up (void)
{
  brightness += 1;
  return REDRAW;
}

EvReaction cmd_brightness_down (void)
{
  brightness -= 1;
  return REDRAW;
}

EvReaction cmd_left_small (void)
{
  x_offset = x_offset - (desired_height* JUMPSMALLLEN) * factor;
  if (x_offset < 0)
    x_offset = 0;
  return REDRAW;
}

EvReaction cmd_grayscale (void)
{
  grayscale = !grayscale;
  return REDRAW;
}

EvReaction cmd_bw (void)
{
  tfb.bw = !tfb.bw;
  return REDRAW;
}

EvReaction cmd_do_dither (void)
{
  tfb.do_dither = !tfb.do_dither;
  return REDRAW;
}

EvReaction cmd_rotate (void)
{
  rotate += 90;
  while (rotate>180)
    rotate-=180;
  return REDRAW;
}

int read_number (void)
{
  char buf[10];
  int count = 0;
  int length = 0;
  int val = 0;

  fprintf (stderr, "[K");

  while ((length=read (STDIN_FILENO, &buf[0], 10)) >= 0)
  {
     if (buf[0] == 127)
     {
        if (count>0)
        {
          printf ("\b \b");
          val /= 10;
          fflush (NULL);
          count--;
        }
     } else
     if (buf[0] == 10)
     {
        return val;
     } else
     {
       if (buf[0] >= '0' && buf[0] <= '9')
       {
         val = val * 10 + (buf[0]-'0');
         fprintf (stdout, "%c", buf[0]);
         fflush(NULL);
         count++;
       }
     }
  }
  return -1; // not reached
}

EvReaction cmd_set_zoom (void)
{
  int val;
  fprintf (stderr, "\rset zoom: ");
  val = read_number ();

  x_offset = 0;
  y_offset = 0;

  factor = 100.0 / val;

  return REDRAW;
}

EvReaction cmd_jump (void)
{
  int val;
  fprintf (stderr, "\rjump to: ");
  val = read_number ();
  image_no = val - 1;
  if (image_no >= images_c)
  image_no = images_c - 1;
  return RELOAD;
}

EvReaction cmd_set_delay (void)
{
  int val;
  fprintf (stderr, "\rnew delay: ");
  val = read_number ();
  delay = val;
  return REDRAW;
}

EvReaction cmd_slideshow (void)
{
  slideshow = !slideshow;
  return REDRAW;
}

EvReaction cmd_thumbs (void)
{
  thumbs = !thumbs;
  return REDRAW;
}

EvReaction cmd_verbosity (void)
{
  verbosity ++;
  if (verbosity > 3)
    verbosity = 0;
  return REDRAW;
}

#define ZOOM_FACTOR 1.33333
#define ZOOM_FACTOR_SMALL 1.05

EvReaction cmd_zoom_in_small (void)
{
  if (thumbs)
  {
    DIVISOR /= ZOOM_FACTOR_SMALL;
    return REDRAW;
  }

  if (x_offset == 0.0 && y_offset == 0.0)
  {
    factor /= ZOOM_FACTOR_SMALL;
  }
  else
  {
    x_offset += desired_width * 0.5 * factor ;
    y_offset += desired_height * 0.5 * factor ;

    factor /= ZOOM_FACTOR_SMALL;

    x_offset -= desired_width * 0.5 * factor ;
    y_offset -= desired_height * 0.5 * factor ;
  }

  return REDRAW;
}

EvReaction cmd_zoom_out_small (void)
{
  if (thumbs)
  {
    DIVISOR *= ZOOM_FACTOR_SMALL;
    return REDRAW;
  }


  x_offset += desired_width * 0.5 * factor ;
  y_offset += desired_height * 0.5 * factor ;

  factor *= ZOOM_FACTOR_SMALL;

  x_offset -= desired_width * 0.5 * factor ;
  y_offset -= desired_height * 0.5 * factor ;
  return REDRAW;
}

EvReaction cmd_zoom_in (void)
{
  if (thumbs)
  {
    DIVISOR /= ZOOM_FACTOR;
    return REDRAW;
  }

  if (x_offset == 0.0 && y_offset == 0.0)
  {
    factor /= ZOOM_FACTOR;
  }
  else
  {
    x_offset += desired_width * 0.5 * factor ;
    y_offset += desired_height * 0.5 * factor ;

    factor /= ZOOM_FACTOR;

    x_offset -= desired_width * 0.5 * factor ;
    y_offset -= desired_height * 0.5 * factor ;
  }

  return REDRAW;
}

EvReaction cmd_zoom_out (void)
{
  if (thumbs)
  {
    DIVISOR *= ZOOM_FACTOR;
    return REDRAW;
  }
  x_offset += desired_width * 0.5 * factor ;
  y_offset += desired_height * 0.5 * factor ;

  factor *= ZOOM_FACTOR;

  x_offset -= desired_width * 0.5 * factor ;
  y_offset -= desired_height * 0.5 * factor ;
  return REDRAW;
}

EvReaction cmd_zoom_width (void)
{
  x_offset = 0;
  y_offset = 0;

  factor = 1.0 * image_w / desired_width;
  return REDRAW;
}

EvReaction cmd_center (void)
{
  x_offset = -(desired_width - image_w / factor) / 2 * factor;
  y_offset = -(desired_height - image_h / (factor * aspect)) / 2 * (factor * aspect);

  return REDRAW;
}

EvReaction cmd_zoom_fit (void)
{
  cmd_zoom_width ();

  if (factor < 1.0 * image_h / desired_height / aspect)
    factor = 1.0 * image_h / desired_height / aspect;

  return cmd_center ();
}

EvReaction cmd_zoom_fill (void)
{
  cmd_zoom_width ();

  if (factor > 1.0 * image_h / desired_height)
    factor = 1.0 * image_h / desired_height;

  return cmd_center ();
}

EvReaction cmd_zoom_1 (void)
{
  x_offset = 0;
  y_offset = 0;

  factor = 1.0;
  return REDRAW;
}

EvReaction cmd_pal_up (void)
{
  tfb.palcount ++;
  return REDRAW;
}

EvReaction cmd_pal_down (void)
{
  tfb.palcount --;
  if (tfb.palcount < 2)
    tfb.palcount = 2;
  return REDRAW;
}

EvReaction cmd_quit (void)
{
  return REQUIT;
}

EvReaction cmd_next (void)
{
  if (image)
    free (image);
  image = NULL;
  image_no ++;
  if (image_no >= images_c)
  {
    if (loop)
      image_no = 0;
    else
      image_no = images_c - 1;
  }
  factor = -1;
  time_remaining = delay;
  return RELOAD;
}

EvReaction cmd_prev (void)
{
  if (image)
    free (image);
  image = NULL;
  image_no --;
  if (image_no < 0)
    image_no = 0;
  factor = -1;
  time_remaining = delay;
  return RELOAD;
}

Action actions[] = {
  {"[1;2A",  cmd_up_small},
  {"[1;2B",  cmd_down_small},
  {"[1;2C",  cmd_right_small},
  {"[1;2D",  cmd_left_small},
  {"[A",     cmd_up},
  {"[B",     cmd_down},
  {"[C",     cmd_right},
  {"[D",     cmd_left},
  {"OA",     cmd_up},
  {"OB",     cmd_down},
  {"OC",     cmd_right},
  {"OD",     cmd_left},
  {" ",        cmd_next},
  {"",       cmd_prev},
  {"[5~",    cmd_prev},
  {"[6~",    cmd_next},
  {"d",        cmd_do_dither},
  {"g",        cmd_grayscale},
  {"s",        cmd_slideshow},
  {"t",        cmd_thumbs},
  {"\t",       cmd_thumbs},
  {"v",        cmd_verbosity},
  {"f",        cmd_zoom_fit},
  {"F",        cmd_zoom_fill},
  {"w",        cmd_zoom_width},
  {"1",        cmd_zoom_1},
  {"+",        cmd_zoom_in},
  {"=",        cmd_zoom_in},
  {"-",        cmd_zoom_out},
  {"a",        cmd_zoom_in_small},
  {"z",        cmd_zoom_out_small},
  {"A",        cmd_zoom_in},
  {"Z",        cmd_zoom_out},
  {"p",        cmd_pal_up},
  {"P",        cmd_pal_down},
  {"q",        cmd_quit},
  {"b",        cmd_bw},
  {"r",        cmd_rotate},
  {"?",        cmd_help},
  {"j",        cmd_jump},
  {"x",        cmd_set_zoom},
  {"S",        cmd_set_delay},

  {".",        cmd_brightness_up},
  {",",        cmd_brightness_down},
  {NULL, NULL}
};

int stdin_got_data (int usec)
{
   struct timeval tv;
   fflush (NULL);
   fd_set rfds;
   FD_ZERO (&rfds);
   FD_SET (STDIN_FILENO, &rfds);
   tv.tv_sec = 0; tv.tv_usec = usec;
   return select (1, &rfds, NULL, NULL, &tv) == 1;
}

EvReaction handle_input (void)
{
   if (stdin_got_data (1))
   {
     char buf[10];
     int length = 0;
     if ((length=read (STDIN_FILENO, &buf[0], 6)) >= 0)
     {
       buf[length]='\0';
       for (int i = 0; actions[i].input; i++)
         if (!strncmp (actions[i].input, buf, strlen(actions[i].input)))
           return actions[i].handler();

       if (verbosity > 1)
       {
         int i = 0;
         if (!message)
         {
           message = malloc (1200);
           message[0]=0;
         }
         message_ttl = 4;

         sprintf (&message[strlen(message)], "%c(%i)", buf[0]>=32?buf[0]:' ', buf[0]);
         for (i = 1; i < length; i++)
           sprintf (&message[strlen(message)], "%c", buf[i]>32?buf[i]:' ');
         return REDRAW;
       }

       return REEVENT;
     }
  }
  return REIDLE;
}

const char *prepare_pdf_page (const char *path, int page_no)
{
  char command[4096];
  sprintf (command, "gs -sDEVICE=pnggray -sOutputFile=/tmp/sxv-pdf.png -dFirstPage=%d -dLastPage=%d -dBATCH -dNOPAUSE -r200 -dTextAlphaBits=4 -dGraphicsAlphaBits=4 %s 2>&1 > /dev/null", page_no, page_no, path);
  system (command);
  return "/tmp/sxv-pdf.png";
}

void print_status (void)
{
  int cleared = 0;

#define CLEAR if (cleared == 0) {\
    cleared = 1;\
    printf ( "[%d;%dH[2K", status_y, status_x);\
  }\

#if 0
  printf ("v:%d ", verbosity);
#endif

  if (message)
  {
    CLEAR

    printf ("%s |", message);
    if (message_ttl -- <= 0)
    {
      free (message);
      message = NULL;
      message_ttl = 0;
    }
  }

  {
    if (verbosity == 0) return;

    CLEAR

    if (verbosity > 0)
    {
      printf ("%i/%i", image_no+1, images_c);

      if (slideshow)
      {
        printf ("[%2.1f]", time_remaining);
      }

      {
        char *a;
        if (pdf) a = strdup (pdf_path);
        else a = strdup (path);
        printf (" %s", basename (a));
        free (a);
      }

    }

    if (verbosity > 1)
    {
      printf (" %ix%i", image_w, image_h);
      printf (" %.0f%%",100.0/factor);
    }

    if (verbosity > 1)
    {
      if (grayscale)
        printf (" -g");
      if (!tfb.do_dither)
        printf (" -nd");

      printf (" -p %d", tfb.palcount);
    }
  }
}

void parse_args (Tfb *tfb, int argc, char **argv)
{
  int x;
  for (x = 1; argv[x]; x++)
  {
    if (!strcmp (argv[x], "--help") || !strcmp (argv[x], "--help"))
    {
      usage ();
    }
    else if (!strcmp (argv[x], "-bw"))
    {
      cmd_bw ();
    }
    else if (!strcmp (argv[x], "-term256"))
    {
      tfb->term256 = 1;
      tfb->do_dither = 1;
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
      tfb->do_dither = 0;
    }
    else if (!strcmp (argv[x], "-t"))
    {
      cmd_thumbs ();
    }
    else if (!strcmp (argv[x], "-s"))
    {
      cmd_slideshow ();
    }
    else if (!strcmp (argv[x], "-x"))
    {
      if (!argv[x+1])
        exit (-1);
      x_offset = strtod (argv[x+1], NULL);
      x++;
    }
    else if (!strcmp (argv[x], "-y"))
    {
      if (!argv[x+1])
        exit (-1);
      y_offset = strtod (argv[x+1], NULL);
      x++;
    }
    else if (!strcmp (argv[x], "-m"))
    {
      if (!argv[x+1])
        exit (-1);

      if (!strcmp (argv[x+1], "ascii"))
        tfb->tv_mode = TV_ASCII;
      else if (!strcmp (argv[x+1], "utf8"))
        tfb->tv_mode = TV_UTF8;
      else if (!strcmp (argv[x+1], "sixel-hi"))
      {
        tfb->tv_mode = TV_SIXEL_HI;
        tfb->palcount = 255;
      }
      else if (!strcmp (argv[x+1], "sixel"))
        tfb->tv_mode = TV_SIXEL;
      else if (!strcmp (argv[x+1], "fb"))
        tfb->tv_mode = TV_FB;
      else
      {
        fprintf (stderr, "invalid argument for -m, '%s' only know of ascii, utf8, sixel and fb\n", argv[x+1]);
        exit(-2);
      }
      x++;
    }
    else if (!strcmp (argv[x], "-p"))
    {
      if (!argv[x+1])
        exit (-1);
      tfb->palcount = atoi (argv[x+1]);
      if (tfb->palcount < 2)
        tfb->palcount = 2;
      x++;
    }
    else if (!strcmp (argv[x], "-w"))
    {
      if (!argv[x+1])
        exit (-2);
      desired_width = atoi (argv[x+1]);
      x++;
    }
    else if (!strcmp (argv[x], "-d"))
    {
      if (!argv[x+1])
        exit (-2);
      delay = strtod (argv[x+1], NULL);
      x++;
    }
    else if (!strcmp (argv[x], "-i"))
    {
      interactive = 0;
    }
    else if (!strcmp (argv[x], "-h"))
    {
      if (!argv[x+1])
        exit (-2);
      desired_height = atoi (argv[x+1]);
      x++;
    }
    else
    {
      images[images_c++] = argv[x];
    }
  }
}
unsigned char *
image_load (const char *path,
            int        *width,
            int        *height,
            int        *stride);

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
                     int                  rotate);

void fill_rect (unsigned char *rgba,
                int            outw,
                int            outh,
                int            outs,
                int            r, int g, int b)
{
  int y, x;
  /* do resampling as part of view, not as a separate step */
  for (y = 0; y < outh; y++)
  {
    int i = y * outs;
    for (x = 0; x < outw; x ++)
    {
      rgba[i + 0] = r;
      rgba[i + 1] = g;
      rgba[i + 2] = b;
      rgba[i + 3] = 255;
      i+= 4;
    }
  }
}

int sixel_is_supported (void);

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image_write.h"

int is_file (const char *path)
{
  struct stat stat_buf;
  if (stat (path, &stat_buf)==0 &&
      S_ISREG(stat_buf.st_mode))
    return 1;
  return 0;
}

void
make_thumb (const char *path, uint8_t *rgba, int w, int h, int dim)
{
  char resolved[4096];
  if (!realpath (path, resolved))
    strcpy (resolved, path);

  if (is_file (resolved))
    return;

  while (strrchr (resolved, '/'))
  {
    *strrchr (resolved, '/') = '\0';
    mkdir (resolved, S_IRWXU);
  }

  uint8_t *trgba = malloc (dim * dim * 4);
  float f = dim / (w * 1.0);
  if (f > dim / (h * 1.0))
    f = dim / (h * 1.0);

  resample_image (rgba, w, h,
                  trgba,
                  w * f,
                  h * f,
                  ((int)(w*f)) * 4,
                  0.0,
                  0.0,
                  1.0/f,
                  1.0,
                  rotate);

  if (!realpath (path, resolved))
    strcpy (resolved, path);
  stbi_write_png (resolved, w *f, h*f, 4, trgba, floor(w * f) * 4);
  free (trgba);
}

void make_thumb_path (const char *path, char *thumb_path)
{
  char resolved[4096];
  if (!realpath (path, resolved))
    strcpy (resolved, path);
  sprintf (thumb_path, "/tmp/tv%s", resolved);
}

void
gen_thumb (const char *path, uint8_t *rgba, int w, int h)
{
  char thumb_path[4096];
  make_thumb_path (path, thumb_path);
  make_thumb (thumb_path, rgba, w, h, 128);
}

int clear = 0;

static inline float mask_a (int x, int y, int c)
{
  return ((((x + c * 67) + y * 236) * 119) & 255 ) / 128.0 / 2;
}

void redraw()
{
  int outw = desired_width;
  int outh = desired_height;
               
  unsigned char *rgba = calloc (outw * 4 * outh, 1);

  if(!thumbs && image)
  {
     resample_image (image, 
                  image_w,
                  image_h,
                  rgba,
                  outw,
                  outh,
                  outw * 4,
                  floor(x_offset),
                  floor(y_offset),
                  factor,
                  aspect,
                  rotate);

     if (brightness != 0)
     {
       int i = 0;
       for (int y = 0; y < outh; y++)
         for (int x = 0; x < outw; x++)
         {
           /* we uses a 2x2 sized dither mask - the dither targets quarter blocks */
           rgba[i+0] += brightness;
           rgba[i+1] += brightness;
           rgba[i+2] += brightness;

           i+=4;
         }

     }

     if (tfb.bw && tfb.do_dither)
     {
       int i = 0;
       for (int y = 0; y < outh; y++)
         for (int x = 0; x < outw; x++)
         {
           /* we uses a 2x2 sized dither mask - the dither targets quarter blocks */
           int val = rgba[i+1] + mask_a(x/2, y/2, 0) * 100 - 50;
           if (val > 128)
           {
             rgba[i+0]=255;
             rgba[i+1]=255;
             rgba[i+2]=255;
           }
           else
           {
             rgba[i+0]=0;
             rgba[i+1]=0;
             rgba[i+2]=0;
           }
           i+= 4;
         }
     }
     else if (tfb.do_dither)
     {
       int i = 0;
       for (int y = 0; y < outh; y++)
         for (int x = 0; x < outw; x++)
         {
           for (int c = 0; c < 3; c++)
           {
             /* we uses a 2x2 sized dither mask - the dither targets quarter blocks */
             int val = rgba[i+c] + mask_a(x/2, y/2, c) * 256/6 - 0.5;
             val = val * 6 / 255;
             rgba[i+c]=val * 255 / 6;
           }
           i+= 4;
         }

     }
  }


  if (thumbs)
  {
  int x = 10;
  int y = 10 - y_offset_thumb;

  for (int i = 0; i < images_c && images[i] && y < outh; i ++)
  {
    char thumb_path[4096];
    make_thumb_path (images[i], thumb_path);
    int image_w, image_h;

    uint8_t *image = 
            is_file (thumb_path) && y >=0 ?
            image_load (thumb_path, &image_w, &image_h, NULL) : NULL;

    if (image && y >= 0)
    {
     float factor = (1.0 * outw/DIVISOR) / image_w;
     int h = outh - y;
     if (h > image_h * (outw/DIVISOR)/image_w / aspect - 4)
       h = image_h * (outw/DIVISOR)/image_w / aspect - 4;

     if (i == image_no)
     {
        if(0)fill_rect(rgba + (outw * (y-2) + (x-2)) * 4,
                  outw/DIVISOR+4, h+4, outw * 4,
                  0,0,0);
        fill_rect(rgba + (outw * (y-1) + (x-1)) * 4,
                  outw/DIVISOR+2, h+2, outw * 4,
                  255,0,0);
     }
     resample_image (image, image_w, image_h,
                     rgba + (outw * y + x) * 4,
                     outw/DIVISOR, h, outw * 4,
                     0, 0, 1.0/factor, aspect, 0);
     free (image);
    }
    else
    {
    }


     x += outw/DIVISOR * 1.1;
     if (x + outw/DIVISOR * 1.1 > outw)
     {
        x = 10;
        y += outw/DIVISOR * 1.1 / aspect;
     }
  }
  }

  if (clear)
  {
    fprintf (stderr, "c");
    clear = 0;
  }

  paint_rgba (&tfb, rgba, outw, outh);

  if (image && !thumbs)
  gen_thumb(path, image, image_w, image_h);

  free (rgba);
}

static long spider_count = 0;
static int ftw_cb (const char *path, const struct stat *info, const int typeflag)
{
  if (!strstr (path, "/tmp/") && (strstr (path, ".png") ||
      strstr (path, ".PNG") ||
      strstr (path, ".gif") ||
      strstr (path, ".GIF") ||
      strstr (path, ".jpg") ||
      strstr (path, ".JPG")))
  {
    images[images_c++]=strdup(path);
    images[images_c]=0;
  }
  spider_count ++;
  return 0;
}

int
main (int argc, char **argv)
{
  /* we initialize the terminals dimensions as defaults, before the commandline
     gets to override these dimensions further 
   */
  init (&tfb, &desired_width, &desired_height);
  parse_args (&tfb, argc, argv);
  time_remaining = delay;   /* do this initialization after
                               argument parsing has settled down */
  images[images_c] = NULL;

  if (images_c <= 0)
    {
      /* no arguments, launch the image viewer */
      /*usage ();*/

      ftw ("/home", ftw_cb, 40);
      //ftw ("/media", ftw_cb, 40);
      //ftw ("/usr/share/wallpapers", ftw_cb, 40);
              fprintf (stderr, "%li paths examined\n", spider_count);
      verbosity = -1;
      slideshow = 1;
      delay = 7;
    }

  if (strstr (images[0], ".pdf") ||
      strstr (images[0], ".PDF"))
    {
      FILE *fp;
      char command[4096];
      command[4095]=0;
      pdf_path = images[0];
      pdf = 1;
      interactive = 1;
      snprintf (command, 4095, "gs -q -c '(%s) (r) file runpdfbegin pdfpagecount = quit' -dNODISPLAY 2>&1", pdf_path);

      fp = popen(command, "r");
      if (fp == NULL)
      {
        fprintf (stderr, "failed to run ghostscript to count pages in PDF %s, aborting\n", pdf_path);
        exit (-1);
      }
      fgets(command, sizeof(command), fp);
      pdf = images_c = atoi (command);

      if (verbosity > 2)
        fprintf (stderr, "%i pages in pdf %s\n", images_c, pdf_path);
      pclose (fp);
    }

  if (interactive)
  {
    zero_origin = 1;
    _nc_raw();
  }

  message = strdup ("press ? or h for help");
  message_ttl = 2;

  for (image_no = 0; image_no < images_c; image_no++)
  {
    interactive_load_image:
    interactive_again:
    if (0){}

    if (!image && !thumbs)
    {
      if (pdf)
        path = prepare_pdf_page (pdf_path, image_no+1);
      else
        path = images[image_no];

      if (image)
        free (image);
      image = NULL;
      image = image_load (path, &image_w, &image_h, NULL);
      if (!image)
      {
        cmd_next ();
        goto interactive_load_image;
      }
      tfb.tv_mode = init (&tfb, &desired_width, &desired_height);

      if (factor < 0)
      {
        cmd_zoom_fill ();
        if (pdf)
        {
          x_offset = 0.0;
          y_offset = 0.0;
        }
      }
    }

    redraw ();

    if (interactive)
    {
      ev_again:
      print_status ();
      switch (handle_input())
        {
          case REQUIT:  printf ("."); exit(0); break;
          case REDRAW: 
          case RELOAD:  goto interactive_again;
          case REEVENT: goto ev_again;
          case RENONE:
          case REIDLE:
            usleep (0.10 * 1000.0 * 1000.0);
            if (slideshow)
            {
              time_remaining -= 0.1;
              if (time_remaining < 0.0)
              {
                cmd_next ();
                goto interactive_load_image;
              }
            }
            goto ev_again;
            break;
        }
    }
    else
    {
      if (image_no < images_c - 1)
      {
        usleep (delay * 1000.0 * 1000.0);
        printf ("\n");
      }
    }
  }

  if (image)
    free (image);
  image = NULL;
  printf ("\r");
  return 0;
}

#include "glyphs.inc"

/* XXX: why do I get compile errors for winsixe struct not having known size when init is moved to paint.c ?*/
TvOutput init (Tfb *tfb, int *dw, int *dh)
{
  struct winsize size = {0,0,0,0};
  int result = ioctl (0, TIOCGWINSZ, &size);
  if (result) {
    *dw = 400; /* default dims - works for xterm which is small*/
    *dh = 300;
  }
  else
  {
    *dw = size.ws_xpixel ;
    *dh = size.ws_ypixel - (1.0*(size.ws_ypixel ))/(size.ws_row + 1) - 12;
    status_y = size.ws_row;
  }


  if (sixel_is_supported () && tfb->tv_mode == TV_AUTO)
  {
    if (getenv("TERM") && !strcmp(getenv("TERM"), "mlterm"))
    {
      if (tfb->palcount == -1) /* only do the autobump on 16, means that it doesn't
                             work for 16 directly  */
        tfb->palcount = 255;
        tfb->do_dither = 1;
      return TV_SIXEL_HI;
    }
    if (tfb->palcount == -1)
      tfb->palcount = 16;
    tfb->do_dither = 1;
    return TV_SIXEL;
  }

  if (tfb->tv_mode == TV_FB ||
      (            
      tfb->tv_mode == TV_AUTO && !getenv("DISPLAY") && getenv("TERM") && !strcmp (getenv ("TERM"), "linux")))
  {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    int fb_fd = open ("/dev/fb0", O_RDWR);

    ioctl (fb_fd, FBIOGET_FSCREENINFO, &finfo);
    ioctl (fb_fd, FBIOGET_VSCREENINFO, &vinfo);


    *dw = vinfo.xres;
    *dh = vinfo.yres;

    tfb->fb_bpp = vinfo.bits_per_pixel;
    if (tfb->fb_bpp == 16)
    {
      tfb->do_dither = 1;
      tfb->fb_bpp = vinfo.red.length +
               vinfo.green.length +
               vinfo.blue.length;
    }

    if (tfb->fb_bpp == 8)
    {
      int i;
      unsigned short red[256], green[256], blue[256];
      struct fb_cmap cmap = {0, 256, red, green, blue, NULL};
      
      if (ocmap_used == 0)
      {
        ioctl (fb_fd, FBIOGETCMAP, &ocmap);
        ocmap_used = 1;
      }
      i=0;
      for (int r = 0; r < 6; r++)
      for (int g = 0; g < 7; g++)
      for (int b = 0; b < 6; b++)
      {
        red[i]   = (r / 5.0) * 65535;
        green[i] = (g / 6.0) * 65535;
        blue[i] =  (b / 5.0) * 65535;
        i++;
      }
      ioctl (fb_fd, FBIOPUTCMAP, &cmap);
      tfb->do_dither = 1;
    }

    tfb->fb_stride = finfo.line_length;
    tfb->fb_mapped_size = finfo.smem_len;

    close (fb_fd);

    return TV_FB;
  }

  if (tfb->tv_mode == TV_ASCII || tfb->tv_mode == TV_UTF8 || (*dw <=0 || *dh <=0))
  {
    if (tfb->tv_mode == TV_UTF8 || tfb->tv_mode == TV_AUTO)
    {
      *dw = size.ws_col * GLYPH_WIDTH;
      *dh = size.ws_row * GLYPH_HEIGHT;
    }
    else
    {
      *dw = size.ws_col * 2;
      *dh = size.ws_row * 2;
    }

    if (tfb->tv_mode == TV_ASCII)
      aspect = 2.0;
    else
      aspect = GLYPH_ASPECT;
    if (tfb->tv_mode != TV_AUTO)
      return tfb->tv_mode;
    return TV_UTF8;
    return TV_ASCII;
  }

  return TV_SIXEL;
}
