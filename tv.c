#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <libgen.h>
#include <sys/time.h>

#define JUMPLEN 0.50
#define JUMPSMALLLEN 0.05

//#define DELTA_FRAME 1

// DELTA_FRAMES works with xterm but are slow, with mlterm each sixel context
// starts off with background-color colored data, rather than the original data
// of the framebuffer at the location.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <jpeglib.h>
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
int rotate = 90;

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
  sixel_out_str ("[?80h");
  sixel_out_str ( "P9;0;q");
}

void term_home ()
{
  //sixel_out_str ( "c");
  sixel_out_str ( "[1H");
}

static float mask_a (int x, int y, int c)
{
  return ((((x + c * 67) + y * 236) * 119) & 255 ) / 128.0 / 2;
}

char *images[4096]={0,};
const char *pdf_path = NULL;
int images_c = 0;

#include <sys/ioctl.h>

int status_y = 25;
int status_x = 1;

#ifdef DELTA_FRAME
int *fb = NULL;
#endif
void init (int *dw, int *dh)
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
#ifdef DELTA_FRAME
  if (fb)
    free (fb);
  fb = malloc (sizeof(int)* *dw * (*dh + 4) );
  for (int i =0; i < *dw * *dh; i++)
    fb[i] = -1;
#endif
}

void usage ()
{
  printf ("usage: tv [--help] [-w <width>] [-h <height>] [-o] [-v] [-g] [-p <palcount>] [-nd] <image1|pdf> [image2 [image3 [image4 ...]]]\n");
  printf ("options:\n");
  printf ("  --help  print this help\n");
  printf ("  -w <int>width in pixels (default terminal width)\n");
  printf ("  -h <int>height in pixels (default terminal iheight)\n");
  printf ("  -o      reset cursor to 0,0 after each drawing\n");
  printf ("  -v      be verbose\n");
  printf ("  -i      interactive interface; use cursors keys, space/backspace etc.\n");
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
int slideshow = 0;
int loop = 0;
float delay = 4.0;
float time_remaining = 0.0;
int verbosity = 0;
int desired_width =  1024;
int desired_height = 1024;
int image_no;
unsigned char *image = NULL;
int image_w, image_h;
const char *path = NULL;
int         pdf = 0;
int zero_origin = 0;
int interactive = 0;

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
    message_ttl = 1;
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

EvReaction cmd_slideshow (void)
{
  slideshow = !slideshow;
  return REDRAW;
}

EvReaction cmd_verbosity (void)
{
  verbosity ++;
  if (verbosity > 2)
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
  y_offset = -(desired_height - image_h / factor) / 2 * factor;

  return REDRAW;
}

EvReaction cmd_zoom_fit (void)
{
  cmd_zoom_width ();

  if (factor < 1.0 * image_h / desired_height)
    factor = 1.0 * image_h / desired_height;

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
    image_no = images_c - 1;
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
  {"[A",  cmd_up},
  {"[B",  cmd_down},
  {"[C",  cmd_right},
  {"[D",  cmd_left},
  {" ",     cmd_next},
  {"",    cmd_prev},
  {"[5~", cmd_prev},
  {"[6~", cmd_next},
  {"d",     cmd_do_dither},
  {"g",     cmd_grayscale},
  {"s",     cmd_slideshow},
  {"v",     cmd_verbosity},
  {"f",     cmd_zoom_fit},
  {"F",     cmd_zoom_fill},
  {"w",     cmd_zoom_width},
  {"1",     cmd_zoom_1},
  {"+",     cmd_zoom_in},
  {"=",     cmd_zoom_in},
  {"-",     cmd_zoom_out},
  {"a",     cmd_zoom_in_small},
  {"z",     cmd_zoom_out_small},
  {"A",     cmd_zoom_in},
  {"Z",     cmd_zoom_out},
  {"p",     cmd_pal_up},
  {"P",     cmd_pal_down},
  {"q",     cmd_quit},
  {"r",     cmd_rotate},
  {"?",     cmd_help},
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
   tv.tv_sec = 0; tv.tv_usec = 1;
   retval = select (1, &rfds, NULL, NULL, &tv);
   if (retval == 1)
   {
     char buf[10];
     int length = 0;
     if ((length=read (STDIN_FILENO, &buf[0], 8)) >= 0)
     {
       buf[length]='\0';
       for (int i = 0; actions[i].input; i++)
         if (!strcmp (actions[i].input, buf))
           return actions[i].handler();

       if (verbosity > 2)
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
  sixel_outf ( "[%d;%dH[2K", status_y, status_x);
  sixel_outf ("                                      \r");
  if (message)
  {
    sixel_outf ("%s", message);
    if (message_ttl -- <= 0)
    {
    free (message);
    message = NULL;
    message_ttl = 0;
    }
    return;
  }
  else
  {
    if (verbosity > 0)
    {
      sixel_outf ("%i/%i", image_no+1, images_c);
      sixel_outf (" %.0f%%",100.0/factor);

      if (pdf)
        sixel_outf (" %s", pdf_path);// basename(path));
      else
        sixel_outf (" %s", path);// basename(path));
    }

    if (verbosity > 0)
      sixel_outf (" %ix%i", image_w, image_h);

    if (verbosity > 1)
    {
      if (grayscale)
        sixel_outf (" -g");
      if (slideshow)
        sixel_outf (" -s");
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
      slideshow = 1;
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
      interactive = 1;
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


unsigned char *jpeg_load(const char *filename, int *width, int *height, int *stride)
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

unsigned char *image_load (const char *path, int *width, int *height, int *stride)
{
  if (strstr (path, ".jpg") ||
      strstr (path, ".jpeg") ||
      strstr (path, ".JPG"))
  {
	 return jpeg_load (path, width, height, stride);
  }

  if (strstr (path, ".png") ||
      strstr (path, ".PNG"))
  {
	 return png_load (path, width, height, stride);
  }

  if (strstr (path, ".tga") ||
      strstr (path, ".pgm") ||
      strstr (path, ".gif") ||
      strstr (path, ".GIF") ||
      strstr (path, ".tiff") ||
      strstr (path, ".bmp") ||
      strstr (path, ".tiff"))
    return stbi_load (path, width, height, stride, 4);
  return NULL;
}


int main (int argc, char **argv)
{
  int x, y;
  int red;
  int green;
  int blue;
  int red_max, green_max, blue_max;

  /* we initialize the terminals dimensions as defaults, before the commandline
     gets to override these dimensions further 
   */
  init (&desired_width, &desired_height);

  parse_args (argc, argv);
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
  message_ttl = 3;

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

  init (&desired_width, &desired_height);
  interactive_again:
  if (0){}

  int   RED_LEVELS   = 2;
  int   GREEN_LEVELS = 4;
  int   BLUE_LEVELS  = 2;

  {
    if (palcount      >= 1000)
    { RED_LEVELS = 10; GREEN_LEVELS = 10; BLUE_LEVELS  = 10; }
    else if (palcount >= 729)
    { RED_LEVELS = 9; GREEN_LEVELS = 9; BLUE_LEVELS  = 9; }
    else if (palcount >= 512)
    { RED_LEVELS = 8; GREEN_LEVELS = 8; BLUE_LEVELS  = 8; }
    else if (palcount >= 343)
    { RED_LEVELS = 7; GREEN_LEVELS = 7; BLUE_LEVELS  = 7; }
    else if (palcount >= 252)
    { RED_LEVELS = 6; GREEN_LEVELS = 7; BLUE_LEVELS  = 6; }
    else if (palcount >= 216)
    { RED_LEVELS = 6; GREEN_LEVELS = 6; BLUE_LEVELS  = 6; }
    else if (palcount >= 150)
    { RED_LEVELS = 5; GREEN_LEVELS = 6; BLUE_LEVELS  = 5; }
    else if (palcount >= 125)
    { RED_LEVELS = 5; GREEN_LEVELS = 5; BLUE_LEVELS  = 5; }
    else if (palcount >= 64)
    { RED_LEVELS = 4;  GREEN_LEVELS = 4; BLUE_LEVELS = 4; }
    else if (palcount >= 32)
    { RED_LEVELS  = 3; GREEN_LEVELS = 3; BLUE_LEVELS = 3; }
    else if (palcount >= 24)
    { RED_LEVELS  = 3; GREEN_LEVELS = 4; BLUE_LEVELS = 2; }
    else if (palcount >= 16) /* the most common case */
    { RED_LEVELS  = 2; GREEN_LEVELS = 4; BLUE_LEVELS  = 2; }
    else if (palcount >= 12) 
    { RED_LEVELS  = 2; GREEN_LEVELS = 3; BLUE_LEVELS  = 2; }
    else if (palcount >= 8) 
    { RED_LEVELS  = 2; GREEN_LEVELS = 2; BLUE_LEVELS  = 2; }
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
  
  {
    if (interactive)
      print_status ();

    if (zero_origin)
      term_home ();
    else
      sixel_outf ("\r");

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
      for (y = 0; y < outh; )
      {
        palno=1;
        for (red   = 0; red   < red_max; red++)
        for (blue  = 0; blue  < blue_max; blue++)
        for (green = 0; green < green_max; green++)
        {
          sixel_outf ( "#%d", palno);
          for (x = 0; x < outw; x ++)
          {
            int sixel = 0;
            int v;
            int q0 = x     * factor + x_offset;
            int q1 = (x+1) * factor + x_offset;
            int dithered[4];
            for (v = 0; v < 6; v++) // XXX: the code redithers,
                                    //      instead of dithering to
                                    //      a tempbuf and then blitting that
                                    //      buf to sixel
            {
              int got_coverage = 0;
              int z0;
              int z1;

              z0 = (y + v) * factor + y_offset;
              z1 = (y + v + 1) * factor + y_offset;
              
              int offset;
              switch (rotate)
              {
                case 90:
                  offset = (int)((q0) * image_w + z0)*4;

                  if (q1 < image_h &&
                    z1 < image_w && q0 >= 0 && z0 >= 0)
                  got_coverage = image[offset+3] > 127;
                  break;
                default:
                case 0:
                  offset = (int)((z0) * image_w + q0)*4;

                  if (z1 < image_h &&
                    q1 < image_w && z0 >= 0 && q0 >= 0)
                  got_coverage = image[offset+3] > 127;
                  break;
              }

              if (got_coverage)
              {
                int z, q;
                int c = 0;
                int offset2;
                dithered[0] = 0;
                dithered[1] = 0;
                dithered[2] = 0;
                for (q = q0; q<=q1; q++)
                  for (z = z0; z<=z1; z++)
                  {
                    switch (rotate)
                    {
                      case 90:
                        offset2 = offset + ((q-q0) * image_w + (z-z0))  * 4;
                        break;
                      case 0:
                      default:
                        offset2 = offset + ((z-z0) * image_w + (q-q0))  * 4;
                      break;
                    }
                    dithered[0] += image[offset2 + 0];
                    dithered[1] += image[offset2 + 1];
                    dithered[2] += image[offset2 + 2];
                    c++;
                  }
                dithered[0] /= c;
                dithered[1] /= c;
                dithered[2] /= c;
                if (do_dither)
                {
                  dithered[0] += mask_a (x, y + v, 0) * 255/(RED_LEVELS-1);
                  dithered[1] += mask_a (x, y + v, 1) * 255/(GREEN_LEVELS-1);
                  dithered[2] += mask_a (x, y + v, 2) * 255/(BLUE_LEVELS-1);
                }
                else
                {
                  dithered[0] += 0.5 * 255/(RED_LEVELS-1);
                  dithered[1] += 0.5 * 255/(GREEN_LEVELS-1);
                  dithered[2] += 0.5 * 255/(BLUE_LEVELS-1);
                }

                if (grayscale)
                {
                  dithered[1] = (dithered[0] + dithered[1] + dithered[2])/3;
                  if ((dithered[1] * (GREEN_LEVELS-1) / 255 == green))
                  {
#ifdef DELTA_FRAME
                    if (fb[(y+v) * outw + x] != palno)
                    {
                      fb[(y+v) * outw + x] = palno;
#else
                    {
#endif
                      sixel |= (1<<v);
                    }
                  }
                }
                else
                {
                  if ((dithered[0] * (RED_LEVELS-1)   / 255 == red) &&
                      (dithered[1] * (GREEN_LEVELS-1) / 255 == green) &&
                      (dithered[2] * (BLUE_LEVELS-1)  / 255 == blue)
                      )
#ifdef DELTA_FRAME
                   if (fb[(y+v) * outw + x] != palno)
                   {
                     fb[(y+v) * outw + x] = palno;
#else
                   {
#endif
                     sixel |= (1<<v);
                   }
                }
              }
              else
                if (red == green && green == blue && blue == 0)
                {
#ifdef DELTA_FRAME
                   if (fb[(y+v) * outw + x] != palno)
                   {
                     fb[(y+v) * outw + x] = palno;
#else
                   {
#endif
                     sixel |= (1<<v);
                   }
                }
            }
            sixel_out (sixel);
          }

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
          palno++;
        }
        sixel_nl ();
        y += 6;
        if (interactive)
        {
          switch (handle_input())
          {
            case REQUIT:  sixel_end();printf ("."); exit(0); break;
            case REDRAW:  sixel_end();goto interactive_again;
            case RELOAD:  sixel_end();goto interactive_load_image;
            case REEVENT: 
            case REIDLE:
            case RENONE:
            break;
          }
        }
      }
      sixel_end ();

      //sixel_outf ("\r");

      if (interactive)
      {
        print_status ();

        ev_again:
        switch (handle_input())
        {
          case REQUIT:  printf ("."); exit(0); break;
          case REDRAW:  goto interactive_again;
          case RELOAD:  goto interactive_load_image;
          case REEVENT: goto ev_again;
          case RENONE:
          case REIDLE:
            usleep (0.20 * 1000.0 * 1000.0);
            if (slideshow)
            {
              time_remaining -= 0.2;
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
