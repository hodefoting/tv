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

typedef enum {TV_AUTO,
              TV_ASCII,
              TV_SIXEL,
              TV_SIXEL_HI,
              TV_UTF8,
              TV_FB} TvOutput;

TvOutput tv_mode = TV_AUTO;

float aspect = 1.0;

#define SKIP_FULL_BLANK_ROWS 1
#define JUMPLEN 0.50
#define JUMPSMALLLEN 0.05

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
int rotate = 0;
#if 0
int sixel_out (int sixel)
{
  sixel_out_char (sixel + '?');
}

void sixel_flush (void)
{
}
#else

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
#endif

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

char *images[4096]={0,};
const char *pdf_path = NULL;
int images_c = 0;

#include <sys/ioctl.h>

int status_y = 25;
int status_x = 1;

int loop = 1;
int fb_bpp = 1;
int fb_mapped_size = 1;
int fb_stride = 1;
int palcount = -1;

int sixel_is_supported (void);


static unsigned short ored[256], ogreen[256], oblue[256];
static struct fb_cmap ocmap = {0, 256, ored, ogreen, oblue, NULL};
int ocmap_used = 0;

TvOutput init (int *dw, int *dh)
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


  if (sixel_is_supported () && tv_mode == TV_AUTO)
  {
    if (getenv("TERM") && !strcmp(getenv("TERM"), "mlterm"))
    {
      if (palcount == -1) /* only do the autobump on 16, means that it doesn't
                             work for 16 directly  */
        palcount = 255;
      return TV_SIXEL_HI;
    }
    if (palcount == -1)
      palcount = 16;
    return TV_SIXEL;
  }

  if (tv_mode == TV_FB ||
      (            
      tv_mode == TV_AUTO && !getenv("DISPLAY") && getenv("TERM") && !strcmp (getenv ("TERM"), "linux")))
  {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    int fb_fd = open ("/dev/fb0", O_RDWR);

    ioctl (fb_fd, FBIOGET_FSCREENINFO, &finfo);
    ioctl (fb_fd, FBIOGET_VSCREENINFO, &vinfo);


    *dw = vinfo.xres;
    *dh = vinfo.yres;

    fb_bpp = vinfo.bits_per_pixel;
    if (fb_bpp == 16)
    {
      fb_bpp = vinfo.red.length +
               vinfo.green.length +
               vinfo.blue.length;
    }

    if (fb_bpp == 8)
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
    }

    fb_stride = finfo.line_length;
    fb_mapped_size = finfo.smem_len;

    close (fb_fd);

    return TV_FB;
  }

  if (tv_mode == TV_ASCII || tv_mode == TV_UTF8 || (*dw <=0 || *dh <=0))
  {
    if (tv_mode == TV_UTF8 || tv_mode == TV_AUTO)
    {
      *dw = size.ws_col * 4;
      *dh = size.ws_row * 4;
    }
    else
    {
      *dw = size.ws_col * 2;
      *dh = size.ws_row * 2;
    }

    aspect = 1.9;
    if (tv_mode != TV_AUTO)
      return tv_mode;
    return TV_UTF8;
    return TV_ASCII;
  }

  return TV_SIXEL;
}

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

float          factor   = -1.0;
float          x_offset = 0.0;
float          y_offset = 0.0;
int            do_dither = 1;
int            grayscale = 0;
int            slideshow = 0;
float          delay = 4.0;
float          time_remaining = 0.0;
int            verbosity = 1;
int            desired_width =  1024;
int            desired_height = 1024;
int            image_no;
unsigned char *image = NULL;
int            image_w, image_h;
const char    *path = NULL;
int            pdf = 0;
int            zero_origin = 0;
int            interactive = 1;

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

EvReaction cmd_do_dither (void)
{
  do_dither = !do_dither;
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
  palcount ++;
  return REDRAW;
}

EvReaction cmd_pal_down (void)
{
  palcount --;
  if (palcount < 2)
    palcount = 2;
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
  {"r",        cmd_rotate},
  {"?",        cmd_help},
  {"j",        cmd_jump},
  {"x",        cmd_set_zoom},
  {"S",        cmd_set_delay},
  {NULL, NULL}
};

static int stdin_got_data (int usec)
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
     if ((length=read (STDIN_FILENO, &buf[0], 8)) >= 0)
     {
       buf[length]='\0';
       for (int i = 0; actions[i].input; i++)
         if (!strcmp (actions[i].input, buf))
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
    sixel_outf ( "[%d;%dH[2K", status_y, status_x);\
  }\

#if 0
  printf ("v:%d ", verbosity);
#endif

  if (message)
  {
    CLEAR

    sixel_outf ("%s |", message);
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
      sixel_outf ("%i/%i", image_no+1, images_c);

      if (slideshow)
      {
        sixel_outf ("[%2.1f]", time_remaining);
      }

      {
        char *a;
        if (pdf) a = strdup (pdf_path);
        else a = strdup (path);
        sixel_outf (" %s", basename (a));
        free (a);
      }

    }

    if (verbosity > 1)
    {
      sixel_outf (" %ix%i", image_w, image_h);
      sixel_outf (" %.0f%%",100.0/factor);
    }

    if (verbosity > 1)
    {
      if (grayscale)
        sixel_outf (" -g");
      if (!do_dither)
        sixel_outf (" -nd");

      sixel_outf (" -p %d", palcount);
    }
  }
}

void parse_args (int argc, char **argv)
{
  int x;
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
        tv_mode = TV_ASCII;
      else if (!strcmp (argv[x+1], "utf8"))
        tv_mode = TV_UTF8;
      else if (!strcmp (argv[x+1], "sixel-hi"))
      {
        tv_mode = TV_SIXEL_HI;
        palcount = 255;
      }
      else if (!strcmp (argv[x+1], "sixel"))
        tv_mode = TV_SIXEL;
      else if (!strcmp (argv[x+1], "fb"))
        tv_mode = TV_FB;
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
      palcount = atoi (argv[x+1]);
      if (palcount < 2)
        palcount = 2;
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
                     unsigned char       *rgba,
                     int                  outw,
                     int                  outh,
                     float                x_offset,
                     float                y_offset,
                     float                factor)
{
  int y, x;
  int i = 0;
  /* do resampling as part of view, not as a separate step */
  for (y = 0; y < outh; y++)
  {
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
          case 90:
            offset = (int)((image_h-q0) * image_w + z0)*4;
            if (q1 < image_h &&
                z1 < image_w && q0 >= 0 && z0 >= 0)
              got_coverage = image[offset+3]>127;
              break;
          default:
          case 0:
            offset = (int)((z0) * image_w + q0)*4;
  
            if (z1 < image_h &&
                q1 < image_w && z0 >= 0 && q0 >= 0)
                got_coverage = image[offset+3]>127;
              break;
        }
        accumulated[0] = 0;
        accumulated[1] = 0;
        accumulated[2] = 0;
        accumulated[3] = 0;

        if (got_coverage)
          {
            int z, q;
            int c = 0;
            int offset2;

            if (q1 == q0) q1 = q0+1;
            if (z1 == z0) z1 = z0+1;

            for (q = q0; q<q1; q++)
              for (z = z0; z<z1; z++)
                {
                  switch (rotate)
                  {
                    case 90:
                      offset2 = offset + ((q0-q) * image_w + (z-z0))  * 4;
                      break;
                    case 0:
                    default:
                      offset2 = offset + ((z-z0) * image_w + (q-q0))  * 4;
                      break;
                  }
                  accumulated[0] += image[offset2 + 0];
                  accumulated[1] += image[offset2 + 1];
                  accumulated[2] += image[offset2 + 2];
                  accumulated[3] += image[offset2 + 3];
                  c++;
                }
                accumulated[0] /= c;
                accumulated[1] /= c;
                accumulated[2] /= c;
                accumulated[3] /= c;
          }
       for (int c = 0; c < 4; c++)
         rgba[i * 4 + c] =
            accumulated[c]>255?255:(accumulated[c]<0?0:accumulated[c]);
       i++;
     }
  }
}

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

void dither_rgba (const unsigned char *rgba,
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
            if (do_dither)
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
  sixel_outf ("[1;47H");
  term_get_xy (&x, &y);
  sixel_start ();
  sixel_outf ("#1---------A-");
  sixel_end ();
  sixel_outf ("\r");
  term_get_xy (&xb, &yb);
  sixel_outf ( "[%d;%dH", oy, ox);
  fflush(NULL);
   inited = (y != yb);
  }
  return inited;
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

static inline long coldiff(uint32_t col1, uint32_t col2)
{
  uint8_t *c1 = (void*)&col1;
  uint8_t *c2 = (void*)&col2;

  return ((c1[0]-c2[0])* (c1[0]-c2[0]) +
          (c1[1]-c2[1])* (c1[1]-c2[1]) +
          (c1[2]-c2[2])* (c1[2]-c2[2]));
}

void paint_rgba (uint8_t *rgba, int outw, int outh)
{
      switch (tv_mode)
      {
        case TV_ASCII:
          {
            unsigned int *pal = calloc (outw * 4 * outh * sizeof (int), 1);
            dither_rgba (rgba,
              pal,
              outw * 4,
              outw,
              outh,
              1,
              2,
              0);
            if (interactive == 0 || !stdin_got_data (1))
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
            if (interactive == 0 || !stdin_got_data (1))
            {
              if (interactive)
                term_home ();
              /* quantization used for approximate matches */
              //uint32_t mask = 0xf8fcf8f8;
              uint32_t mask = 0xf0f0f0f0;
              //uint32_t mask = 0xc0c0c0c0;
              //uint32_t mask = 0x80808080;
              //uint32_t mask = 0xffffffff;

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
                    sixel_outf("[48;2;%i;%i;%im", (maxc)&0xff,(maxc >> 8)&0xff  , (maxc >> 16) & 0xff );
                    sixel_outf("[38;2;%i;%i;%im", (secondmaxc)&0xff,(secondmaxc >> 8)&0xff  , (secondmaxc >> 16) & 0xff );
                  }
                  else
                  {
                    sixel_outf("[38;2;%i;%i;%im", (maxc)&0xff,(maxc >> 8)&0xff  , (maxc >> 16) & 0xff );
                    sixel_outf("[48;2;%i;%i;%im", (secondmaxc)&0xff,(secondmaxc >> 8)&0xff  , (secondmaxc >> 16) & 0xff );
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
            unsigned char *fb = mmap (NULL, fb_mapped_size, PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);

            for (scan = 0; scan < outh; scan++)
            {
              unsigned char *src = rgba + outw * 4 * scan;
              unsigned char *dst = fb + fb_stride * scan;
              int x;

		      switch (fb_bpp)
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
            munmap (fb, fb_mapped_size);
            fflush (stdout);
            close (fb_fd);
          }
          break;
        case TV_SIXEL_HI:
        case TV_SIXEL:
          {
      unsigned int *pal = calloc (outw * 4 * outh * sizeof (int), 1);
      dither_rgba (rgba, pal, outw * 4, outw, outh, grayscale, palcount, 0);
      if (!stdin_got_data (1))
      blit_sixel_pal (pal, outw * 4, 0, 0, outw, outh, grayscale, palcount, 0
                  );
      free (pal);
           }
           break;
        case TV_AUTO:
          fprintf (stderr, "uh? %i", __LINE__);
          break;
      }
}

int
main (int argc, char **argv)
{
  /* we initialize the terminals dimensions as defaults, before the commandline
     gets to override these dimensions further 
   */
  init (&desired_width, &desired_height);

  parse_args (argc, argv);
  time_remaining = delay;   /* do this initialization after
                               argument parsing has settled down
                             */
  images[images_c] = NULL;

  if (images_c <= 0)
    {
       usage ();
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
    //fprintf (stderr,"[%s]\n", images[image_no]);

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
    tv_mode = init (&desired_width, &desired_height);

    if (factor < 0)
    {
      cmd_zoom_fill ();
      if (pdf)
      {
        x_offset = 0.0;
        y_offset = 0.0;
      }
    }

    if (!image)
    {
      sixel_outf ("\n\n");
      return -1;
    }

    int clear = 0;

    interactive_again:
    if (0){}

    int outw = desired_width;
    int outh = desired_height;
               
    {
      unsigned char *rgba = calloc (outw * 4 * outh, 1);

      resample_image (image, 
                      rgba,
                      outw,
                      outh,
                      x_offset,
                      y_offset,
                      factor);
      if (clear)
      {
         fprintf (stderr, "c");
         clear = 0;
      }
      paint_rgba (rgba, outw, outh);

      free (rgba);

      if (interactive)
      {
        ev_again:
        print_status ();
        switch (handle_input())
        {
          case REQUIT:  printf ("."); exit(0); break;
          case REDRAW:  goto interactive_again;
          case RELOAD:  goto interactive_load_image;
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
    }
    free (image);
    image = NULL;
    if (image_no < images_c - 1)
    {
      usleep (delay * 1000.0 * 1000.0);
      sixel_outf ("\n");
      cmd_next ();
    }
  }
  sixel_outf ("\r");
  return 0;
}
