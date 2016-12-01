#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <libgen.h>
#include <sys/time.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <ftw.h>
#include "tv.h"

int do_jitter = 0;
float          factor         = -1.0;
float          x_offset       = 0.0;
float          y_offset       = 0.0;
int            slideshow      = 0;
float          delay          = 4.11f;
float          time_remaining = 0.0;
int            verbosity      = 0;
int            desired_width  = 1024;
int            desired_height = 1024;
int            image_no;
unsigned char *image          = NULL;
int            image_w, image_h;
const char    *path           = NULL;
const char    *output_path    = NULL;
int            pdf            = 0;
int            zero_origin    = 0;
int            linear         = 1;
int            set_w          = 0;
int            set_h          = 0;
float          DIVISOR        = 6.0;
int            brightness     = 0;
float          contrast       = 1.0;

static int ftw_cb (const char *path, const struct stat *info, const int typeflag);

Tfb tfb = {
1,1,1,-1,0,0,1,0,0,TV_AUTO
};

enum {
  TV_ZOOM_NONE = 0,
  TV_ZOOM_FIT,
  TV_ZOOM_FILL,
  TV_ZOOM_WIDTH
};

int   zoom_mode = TV_ZOOM_FIT;
float aspect    = 1.0;
int   rotate    = 0;

#define SKIP_FULL_BLANK_ROWS 1
#define JUMPLEN              0.50
#define JUMPSMALLLEN         0.05

/* more images than this - and we give up.. */
char       *images[40960]={0,};
const char *pdf_path = NULL;
int         images_c = 0;

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
  printf ("usage: tv [--help] [-s <widthxheight> -m <mode> [images] -o outputfile\n");
  printf ("options:\n");
  printf ("  --help  print this help\n");
  printf ("  -s <widthxheight>\n");
  printf ("     the dimension is in pixels for sixel modes, and thumbnails creation\n");
  printf ("     and in character cells for ascii/utf8 modes\n");

  //printf ("  -o      reset cursor to 0,0 after each drawing\n");
#if 0
  F - fit
  f - fill
  w - width (and at top)
  XXX: todo if width or height is specified as -1 - do proportional scaling
#endif
  printf ("  -m <mode>  specify no or invalid mode to get a list of valid modes\n");
  printf ("  -r shuffle images for slideshow\n");
  //printf ("  -v      be verbose\n");
  //printf ("  -i      interactive interface; use cursors keys, space/backspace etc.\n");
  printf ("  -d <delay>  set slideshow mode\n");
/*

  printf ("  -g      do a grayscale instead of color\n");
  printf ("  -nd     no dithering\n");
  printf ("  -p <count>  use count number of colors\n");
  */
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
#if 0
  if (tfb.tv_mode == TV_FB){
    int tty_fd = open("/dev/tty0", O_RDWR);
    ioctl (tty_fd, KDSETMODE, KD_TEXT);
    close (tty_fd);
  }
#endif
}

static int _nc_raw (void)
{
  struct termios raw;
  if (!atexit_registered)
    {
      atexit (sixel_at_exit);
      atexit_registered = 1;
    }
  tcgetattr (STDIN_FILENO, &orig_attr);
  raw = orig_attr;  /* modify the original mode */
  raw.c_lflag &= ~(ECHO );
  raw.c_lflag &= ~(ICANON );
  raw.c_cc[VMIN] = 1;  /* 1 byte */
  raw.c_cc[VTIME] = 0; /* no timer */
  tcsetattr (STDIN_FILENO, TCSANOW, &raw);
  nc_is_raw = 1;
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
    y_offset -= (desired_height * JUMPLEN) * factor;
#if 0
  if (y_offset < 0)
    y_offset = 0;
#endif
  return REDRAW;
}

EvReaction cmd_down (void)
{
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

EvReaction cmd_invert (void)
{
  contrast *= -1;
  return REDRAW;
}

EvReaction cmd_contrast_up (void)
{
  contrast *= 1.1;
  return REDRAW;
}

EvReaction cmd_contrast_down (void)
{
  contrast /= 1.1;
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
  tfb.grayscale = !tfb.grayscale;
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

void reset_controls (void);

void drop_image (void)
{
  if (image)
    free (image);
  image = NULL;
  reset_controls ();
}

EvReaction cmd_jump (void)
{
  int val;
  fprintf (stderr, "\rjump to: ");
  val = read_number ();
  image_no = val - 1;
  if (image_no >= images_c)
  image_no = images_c - 1;
  drop_image ();
  return RELOAD;
}

EvReaction cmd_set_delay (void)
{
  int val;
  fprintf (stderr, "\rnew delay: ");
  val = read_number ();
  delay = val;
  time_remaining = delay;
  return REDRAW;
}

EvReaction cmd_slideshow (void)
{
  slideshow = !slideshow;
  return REDRAW;
}

EvReaction cmd_jitter (void)
{
  do_jitter = !do_jitter;
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
  x_offset += desired_width * 0.5 * factor ;
  y_offset += desired_height * 0.5 * factor ;

  factor *= ZOOM_FACTOR_SMALL;

  x_offset -= desired_width * 0.5 * factor ;
  y_offset -= desired_height * 0.5 * factor ;
  return REDRAW;
}

EvReaction cmd_zoom_in (void)
{

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

  factor /= 1.000001; /* fudging - otherwise width ends up wrong */

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

void reset_controls (void)
{
  brightness = 0;
  contrast = 1;
  time_remaining = delay;
  factor = -1;
}

static int rand_shuf (const void *a, const void *b)
{
  return (rand() % 50) - 25;
}

EvReaction cmd_shuffle (void)
{
  drop_image ();
  image_no = 0;

  srand(time(NULL));
  qsort (images, images_c, sizeof (void*), rand_shuf);

  reset_controls ();
  return RELOAD;
}

EvReaction cmd_next (void)
{
  drop_image ();
  image_no ++;
  if (image_no >= images_c)
  {
    if (loop)
      image_no = 0;
    else
      image_no = images_c - 1;
  }
  reset_controls ();
  return RELOAD;
}

EvReaction cmd_prev (void)
{
  drop_image ();
  image_no --;
  if (image_no < 0)
    image_no = 0;
  reset_controls ();
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
  {"r",        cmd_shuffle},
  {"?",        cmd_help},
  {"j",        cmd_jump},
  {"J",        cmd_jitter},
  {"x",        cmd_set_zoom},
  {"S",        cmd_set_delay},

  {".",        cmd_brightness_up},
  {",",        cmd_brightness_down},
  {"<",        cmd_contrast_down},
  {">",        cmd_contrast_up},
  {"i",        cmd_invert},
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
  return "/tmp/tv-pdf.png";
}

void print_status (void)
{
  int cleared = 0;

#define CLEAR if ((cleared == 0) && tfb.interactive) {\
    cleared = 1;\
    printf ( "[%d;%dH[2K", status_y, status_x);\
  }\

    CLEAR
#if 1
  printf ("v:%d:%d ", tfb.tv_mode, sixel_is_supported());
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
      if (tfb.grayscale)
        printf (" -g");
      if (!tfb.do_dither)
        printf (" -nd");

      printf (" -p %d", tfb.palcount);
    }
  }
}

void add_image (Tfb *tfb, const char *path)
{
  images[images_c++] = strdup (path);
  images[images_c] = NULL;
}

int is_dir (const char *path);

void add_path (Tfb *tfb, char *path)
{
  char *p = path;
  if (p[0]!='/')
    p = realpath (path, NULL);

  if (is_dir (p))
  {
    ftw (p, ftw_cb, 40);
  }
  else if (p) 
  {
    add_image (tfb, p);
  }

  if (p != path)
    free (p);
}

int do_shuffle = 0;

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
      tfb->grayscale = 1;
    }
    else if (!strcmp (argv[x], "-nd"))
    {
      tfb->do_dither = 0;
    }
    else if (!strcmp (argv[x], "-j"))
    {
      tfb->do_jitter = 1;
    }
    else if (!strcmp (argv[x], "-dd"))
    {
      tfb->do_dither = 1;
    }
    else if (!strcmp (argv[x], "-d"))
    {
      if (!argv[x+1])
      {
        fprintf (stderr, "-d expected argument, aborting\n");
        exit (-1);
      }
      delay = strtod (argv[x+1], NULL);
      x++;
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
        goto list_modes;

      if (!strcmp (argv[x+1], "ascii"))
      {
        tfb->tv_mode = TV_ASCII;
        tfb->do_dither = 1;
      }
      else if (!strcmp (argv[x+1], "utf8"))
      {
        tfb->tv_mode = TV_UTF8;
      }
      else if (!strcmp (argv[x+1], "utf8-2"))
      {
        tfb->tv_mode = TV_UTF8;
        tfb->bw = 1;
      }
      else if (!strcmp (argv[x+1], "utf8-2-gray"))
      {
        tfb->tv_mode = TV_UTF8;
        tfb->bw = 1;
        tfb->do_dither = 0;
      }
      else if (!strcmp (argv[x+1], "utf8-256"))
      {
        tfb->tv_mode = TV_UTF8;
        tfb->term256 = 1;
      }
      else if (!strcmp (argv[x+1], "utf8-256-gray"))
      {
        tfb->tv_mode = TV_UTF8;
        tfb->term256 = 1;
        tfb->grayscale = 1;
      }
      else if (!strcmp (argv[x+1], "utf8-24"))
      {
        tfb->tv_mode = TV_UTF8;
      }
      else if (!strcmp (argv[x+1], "sixel-256"))
      {
        tfb->tv_mode = TV_SIXEL_HI;
        tfb->palcount = 255;
      }
      else if (!strcmp (argv[x+1], "sixel-16"))
      {
        tfb->tv_mode = TV_SIXEL;
        tfb->palcount = 16;
        tfb->do_dither = 1;
      }
      else if (!strcmp (argv[x+1], "sixel-16-gray"))
      {
        tfb->tv_mode = TV_SIXEL;
        tfb->palcount = 16;
        tfb->grayscale = 1;
        tfb->do_dither = 1;
      }
      else if (!strcmp (argv[x+1], "list"))
      {
        goto list_modes;
      }
      else if (!strcmp (argv[x+1], "sixel"))
      {
        tfb->tv_mode = TV_SIXEL;
      }
      else if (!strcmp (argv[x+1], "fb"))
        tfb->tv_mode = TV_FB;
      else
      {
        list_modes:
        fprintf (stderr, "invalid argument for -m, '%s'\n"
"try one of: \n"
"  ascii\n"
"  utf8\n"
"  utf8-2\n"
"  utf8-2-gray\n"
"  utf8-256\n"
"  utf8-256-gray\n"
"  sixel\n"
"  sixel-16\n"
"  sixel-16-gray\n"
"  sixel-256\n"
"  fb\n",  argv[x+1]);
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
      set_w = atoi (argv[x+1]);
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
      tfb->interactive = 0;
    }
    else if (!strcmp (argv[x], "-h"))
    {
      if (!argv[x+1])
        exit (-2);
      set_h = atoi (argv[x+1]);
      x++;
    }
    else if (!strcmp (argv[x], "-o"))
    {
      char *str = argv[++x];
      if (!str)
      {
        fprintf (stderr, "-o needs a path to place output in\n");
        exit (-2);
      }
      output_path = str;
      aspect = 1;
      tfb->tv_mode = TV_SIXEL;
      tfb->palcount = 16;

      if (set_w == 0)
      {
        set_w = 256;
        set_h = 256;
      }
    }
    else if (!strcmp (argv[x], "-r"))
    {
      do_shuffle = 1;
    }
    else if (!strcmp (argv[x], "-s"))
    {
      char *str = argv[++x];
      if (!str)
      {
        fprintf (stderr, "-s expected argument, aborting\n");
        exit (-2);
      }

      if (!strchr (str, 'x'))
      {
        fprintf (stderr, "-s expected argument with an 'x' as in 160x120\n");
        exit (-2);
      }

      set_w = atoi (str);
      set_h = atoi (strchr (str, 'x') + 1);

      //fprintf (stderr, "set %i x %i\n", set_w, set_h);
    }
    else
    {
      add_path (tfb, argv[x]);
    }
  }
}

int write_jpg
           (char const *filename,
            int w, int h, int comp, const void *data,
            int stride_in_bytes);

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

int is_dir (const char *path)
{
  struct stat stat_buf;
  if (stat (path, &stat_buf)==0 &&
      S_ISDIR(stat_buf.st_mode))
    return 1;
  return 0;
}

static void
mk_ancestry_iter (const char *path)
{
  char copy[4096];
  strncpy (copy, path, 4096);
  if (strrchr (copy, '/'))
  { 
    *strrchr (copy, '/') = '\0';
    if (copy[0])
    {
      struct stat stat_buf;
      if ( ! (stat (copy, &stat_buf)==0 && S_ISDIR(stat_buf.st_mode)))
      {
        mk_ancestry_iter (copy);
#ifndef _WIN32 
        mkdir (copy, S_IRWXU);
#else
        mkdir (copy);
#endif
      }
    }
  }
}

static void
mk_ancestry (const char *path)
{
  char copy[4096];
  strncpy (copy, path, 4096);
#ifdef _WIN32
  for (char *c = copy; *c; c++)
    if (*c == '\\')
      *c = '/';
#endif
  mk_ancestry_iter (copy);
}


void
make_thumb (const char *path, uint8_t *rgba, int w, int h, int dim)
{
  char resolved[4096];
  if (!realpath (path, resolved))
    strcpy (resolved, path);

  if (is_file (resolved))
    return;

  mk_ancestry (resolved);

  uint8_t *trgba = malloc ((dim+2) * (dim+2) * 4);
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
                  rotate,
                  linear);

  if (!realpath (path, resolved))
    strcpy (resolved, path);

  if (strstr (resolved, ".png") ||
      strstr (resolved, ".PNG"))
    stbi_write_png (resolved, floor( w *f), floor(h*f), 4, trgba, floor(w * f) * 4);
  else
    write_jpg (resolved, w *f, h*f, 4, trgba, floor(w * f) * 4);

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
  make_thumb (thumb_path, rgba, w, h, 64);
}

int clear = 0;

static inline float mask_a (int x, int y, int c)
{
  return ((((x + c * 67) + y * 236) * 119) & 255 ) / 128.0 / 2;
}

int frame_no = 0;

#define XJIT  (do_jitter?(((frame_no+1)%4)/2):0)
#define YJIT  (do_jitter?((frame_no%4)%2):0)

void redraw()
{
  int outw = desired_width;
  int outh = desired_height;
               
  unsigned char *rgba = calloc (outw * 4 * outh, 1);

  frame_no ++;

  if(image)
  {
    resample_image (image, 
                    image_w,
                    image_h,
                    rgba,
                    outw,
                    outh,
                    outw * 4,
                    floor(x_offset + XJIT * factor),
                    floor(y_offset + YJIT * factor / aspect),
                    factor,
                    aspect,
                    rotate,
                    linear);

     if (brightness != 0 || contrast != 1.0)
     {
       int i = 0;
#ifdef USE_OPEN_MP
  #pragma omp for
#endif
       for (int y = 0; y < outh; y++)
         for (int x = 0; x < outw; x++)
         {
           int c;
           int val;
           for (c = 0; c < 3; c++)
           {
             val = rgba[i+c] + brightness;
             if (contrast != 1.0)
                val = (val - 127) * contrast + 127;
             if (val < 0)
              rgba[i+c] = 0;
             else if (val > 255)
              rgba[i+c] = 255;
             else
              rgba[i+c] = val;
           }
           i+=4;
         }

     }

     if (tfb.grayscale)
     {
       int i = 0;
#ifdef USE_OPEN_MP
  #pragma omp for
#endif
       for (int y = 0; y < outh; y++)
         for (int x = 0; x < outw; x++)
         {
           int c;
           int val = (rgba[i+0] +
                     rgba[i+1] +
                     rgba[i+2]) / 3;

           for (c = 0; c < 3; c++)
             rgba[i+c] = val;
           i+=4;
         }

     }

  }

  if (output_path)
  {
    if (strstr (output_path, ".png"))
      stbi_write_png (output_path, outw, outh, 4, rgba, outw * 4);
    else
      write_jpg (output_path, outw, outh, 4, rgba, outw * 4);
    exit(0);
  }

     if (tfb.bw && tfb.do_dither)
     {
       int i = 0;
       for (int y = 0; y < outh; y++)
         for (int x = 0; x < outw; x++)
         {
           int val = rgba[i+1];
           
           if (val > 16 && val < 255-16)
             val += mask_a(x/2, y/2, 0) * 100 - 50;
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
           long graydiff = 
             (rgba[i+0] - rgba[i+1]) * (rgba[i+0] - rgba[i+1]) +
             (rgba[i+2] - rgba[i+1]) * (rgba[i+2] - rgba[i+1]) +
             (rgba[i+2] - rgba[i+0]) * (rgba[i+2] - rgba[i+0]);

           if (graydiff < 2000) /* force to gray, and separate dither */
           {
             int val = rgba[i+1] + (mask_a(x/1.66, y/1.66, 0) - 0.5) * 255.0 / 17;

             if (val > 255) val = 255;
             if (val < 0) val = 0;
             for (int c = 0; c < 3; c++)
               rgba[i+c] = val;


           }
           else
           for (int c = 0; c < 3; c++)
           {
             /* we uses a 2x2 sized dither mask - the dither targets quarter blocks */
             int val = rgba[i+c] + (mask_a(x/1.66, y/1.66, 0) - 0.5) * 255.0 / 5;
             if (val > 255) val = 255;
             if (val < 0) val = 0;
             rgba[i+c] = val;
           }
           i+= 4;
         }
     }

  if (clear && tfb.interactive)
  {
    fprintf (stderr, "c");
    clear = 0;
  }

  paint_rgba (&tfb, rgba, outw, outh);
  free (rgba);
}


int ensure_image()
{
    if (!image)
    {
      if (pdf)
        path = prepare_pdf_page (pdf_path, image_no+1);
      else
        path = images[image_no];

      if (image)
        free (image);
      image = NULL;
      image = image_load (path, &image_w, &image_h);
      if (!image)
      {
        cmd_next ();
        return -1;
//        goto interactive_load_image;
      }
  
      if (isatty (STDOUT_FILENO))
      tfb.tv_mode = init (&tfb, &desired_width, &desired_height);

      if (factor < 0)
      {
        switch (zoom_mode)
        {
          case TV_ZOOM_FILL:
            cmd_zoom_fill ();
            break;
          case TV_ZOOM_FIT:
            cmd_zoom_fit ();
            break;
        }
        if (pdf)
        {
          x_offset = 0.0;
          y_offset = 0.0;
        }
      }
    }
  return 0;
}

static long spider_count = 0;
static long img_count = 0;
static int drawn = 0;

static int ftw_cb (const char *path, const struct stat *info, const int typeflag)
{
  if (!strstr (path, "/tmp/") && 
     (strstr (path, ".png") ||
      strstr (path, ".PNG") ||
      strstr (path, ".gif") ||
      strstr (path, ".GIF") ||
      strstr (path, ".jpg") ||
      strstr (path, ".jpeg") ||
      strstr (path, ".JPEG") ||
      strstr (path, ".JPG")))
  {
    add_image (&tfb, path);
    img_count ++;
  }
  spider_count ++;
#if 1

#define SKIP 30
  if ( (spider_count % SKIP) == 0)
  {
    if (images_c && !drawn)
    {
      /* first image is splash - making startup even for full system
         spidered slideshow be instant 
       */
      if (!do_shuffle) // XXX: there might be a better way of
                       //      getting a random of the first
                       //      set, then reshuffle all but first
                       //      image afterwards..
      {
        ensure_image ();
        redraw ();
        drawn = 1;
      }
    }

    if(0)switch ( (spider_count / SKIP) % 4  )
    {
      case 0: fprintf (stdout, "\r- %li images of %li files", img_count, spider_count); break;
      case 1: fprintf (stdout, "\r/ %li images of %li files", img_count, spider_count); break;
      case 2: fprintf (stdout, "\r| %li images of %li files", img_count, spider_count); break;
      case 3: fprintf (stdout, "\r\\ %li images of %li files", img_count, spider_count); break;
    }
  }
#endif
#undef SKIP
  return 0;
}

int
main (int argc, char **argv)
{

  /* we initialize the terminals dimensions as defaults, before the commandline
     gets to override these dimensions further 
   */
  desired_width = 80;
  desired_height = 25;
  parse_args (&tfb, argc, argv);

 if (output_path)
   {
      tfb.interactive = 0;
   }

  if (isatty (STDOUT_FILENO) && tfb.interactive)
    init (&tfb, &desired_width, &desired_height);

 if (output_path)
   {
      if (set_w)
      {
        desired_width = set_w;
        desired_height = set_h;
        aspect = 1.0;
      }
   }

  time_remaining = delay;   /* do this re-initialization after
                               argument parsing has settled down */

  if (tfb.interactive)
  {
    zero_origin = 1;
    _nc_raw();
  }

  images[images_c] = NULL;

  if (images_c <= 0)
    {
      if (getenv ("HOME"))
        add_path (&tfb, getenv ("HOME"));
      add_path (&tfb, "/media");
      slideshow = 1;
    }
  if (images_c <= 0)
    {
      add_path (&tfb, "/usr/share");
    }

  if (do_shuffle)
    cmd_shuffle ();

  /* if the first image is a pdf - do pdf mode instead  */
  if (strstr (images[0], ".pdf") ||
      strstr (images[0], ".PDF"))
    {
      FILE *fp;
      char command[4096];
      command[4095]=0;
      pdf_path = images[0];
      pdf = 1;
      tfb.interactive = 1;
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

  int done = 0;
  while (!done)
  {
    interactive_load_image:
    if (0){}

    if (ensure_image())
      goto interactive_load_image;

    redraw ();
    if (tfb.interactive)
    {
      ev_again:
      print_status ();
      switch (handle_input())
        {
          case REQUIT:  exit(0); break;
          case REDRAW: 
          case RELOAD:  goto interactive_load_image;
          case REEVENT: goto ev_again;
          case RENONE:
          case REIDLE:
            if (do_jitter)
            {
              goto interactive_load_image;
            }

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
        cmd_next ();
      }
      else
      {
        done = 1;
      }
    }
  }

  drop_image ();
  printf ("\r");
  return 0;
}

#include "glyphs.inc"

/* XXX: why do I get compile errors for winsixe struct not having known size when init is moved to paint.c ?*/
TvOutput init (Tfb *tfb, int *dw, int *dh)
{
  struct winsize size = {0,0,0,0};

  if (set_w || set_h)
  {
    *dw = set_w;
    *dh = set_h;
  }
  else
  {
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
    int fb_fd;

    fb_fd = open ("/dev/fb0", O_RDWR);

    ioctl (fb_fd, FBIOGET_VSCREENINFO, &vinfo);

    if (!(set_w || set_h))
    {
      *dw = vinfo.xres;
      *dh = vinfo.yres;
    }
   
    if (tfb->fb_bpp != 1){
      close (fb_fd);
      return TV_FB;
    } 

    ioctl (fb_fd, FBIOGET_FSCREENINFO, &finfo);

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

#if 0
    {
    int tty_fd = open("/dev/tty0", O_RDWR);
    ioctl (tty_fd, KDSETMODE, KD_GRAPHICS);
    close (tty_fd);
    }
#endif

    close (fb_fd);

    return TV_FB;
  }

  if (tfb->tv_mode == TV_ASCII ||
      tfb->tv_mode == TV_UTF8 ||
      (tfb->tv_mode == TV_AUTO && !sixel_is_supported())||
      (*dw <=0 || *dh <=0))
  {
    if (tfb->tv_mode == TV_UTF8 || tfb->tv_mode == TV_AUTO)
    {
      if (set_w || set_h)
      {
        *dw *= GLYPH_WIDTH;
        *dh *= GLYPH_HEIGHT;
      }
      else
      {
        *dw = size.ws_col * GLYPH_WIDTH;
        *dh = size.ws_row * GLYPH_HEIGHT;
      }
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


    /* try to detect if we're supposed to do 256 color,
       if we cannot do 256 color
     */
    if (getenv("TERM"))
      if (!strcmp(getenv("TERM"), "screen")||
          !strcmp(getenv("TERM"), "rxvt-unicode-256color") ||
          !strcmp(getenv("TERM"), "xterm-256color")
         )
      {
        tfb->term256 = 1;
        tfb->do_dither = 1;
      }
    if (getenv("TERM_PROGRAM") &&
        !strcmp(getenv("TERM_PROGRAM"), "Apple_Terminal"))
      {
        tfb->term256 = 1;
        tfb->do_dither = 1;
      }
    if (getenv("COLORTERM") &&
        !strcmp(getenv("COLORTERM"), "truecolor"))
    {
       tfb->term256 = 0;
       tfb->do_dither = 0;
    }


    return TV_UTF8;
    return TV_ASCII;
  }

  return TV_SIXEL;
}
