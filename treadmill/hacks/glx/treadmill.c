/* treadmill, 2010 C.Gallioz
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 *****************************************************************************
 *
 * Displays rolling images on many layers. 
 * This was initially written by modifying glslideshow (Copyright (c) 2003-2008 Jamie Zawinski <jwz@jwz.org>).
 */

#define DEFAULTS  "*delay:           30000                \n" \
                  "*showFPS:         False                \n" \
                  "*fpsSolid:        True                 \n" \
                  "*useSHM:          True                 \n" \
                  "*titleFont:       -*-helvetica-medium-r-normal-*-10-*\n" \
                  "*desktopGrabber:  xscreensaver-getimage -no-desktop %s\n" \
                  "*grabDesktopImages:   False \n" \
                  "*chooseRandomImages:  True  \n"

# define refresh_treadmill 0
# define release_treadmill 0
# include "xlockmore.h"

#undef countof
#define countof(x) (sizeof((x))/sizeof((*x)))

#ifdef USE_GL


/* directions mode */
#define LEFT2RIGHT 1
#define RIGHT2LEFT 2
#define TOP2BOTTOM 3
#define BOTTOM2TOP 4
#define UNIQUE_CARDINAL_DIR 5
#define MULTI_CARDINAL_DIR 6
#define ANY_DIR 7
#define ANY_MODE 8

#define DEF_COVER_TIME     "35"
#define DEF_AMPLITUDE      "75"
#define DEF_IMG_COUNT      "12"
#define DEF_STRETCH_MIN   "15"
#define DEF_STRETCH_MAX   "50"
#define DEF_DIRECTIONS     "6"
#define DEF_CHANGE_DIR     "60"
#define DEF_TITLES         "False"
#define DEF_DEBUG          "False"
#define DEF_DEBUG_LEVEL    "0"
#define DEF_LINE_WIDTH     "1"

#include "grab-ximage.h"
#include "glxfonts.h"

#define BUF_IMAGE_LOADING_MAX 2
#define MAX_LAYERS 3

#ifndef M_PI
#define M_PI 3.14159265358979323846    /* pi */
#endif

#define RAND_MAX_2 (RAND_MAX / 2)

typedef struct {
  double x, y, w, h;
} rect;

typedef struct {
  GLfloat x, y;
} point;

typedef struct {
  ModeInfo *mi;
  int id;              /* unique number for debugging */
  char *title;         /* the filename of this image */
  int w, h;            /* size in pixels of the image */
  XRectangle geom;     /* where in the image the bits are */
  Bool loaded;         /* whether the image has finished loading */
  Bool used;           /* whether the image is appearing on screen */
  Bool useless;        /* whether the image can be destroyed */
  GLuint texid;        /* which texture contains the image */
  rect pos;            /* position of the image. The w and h are */
                       /* actually the half of the actual w and h */
  point tex_point_1;   /* 1st point of the texture diagonal */
  point tex_point_2;   /* 2nd point of the texture diagonal */
  int stretch;         /* stretch of the image */
  double hspeed;       /* horizontal speed */
  double vspeed;       /* vertical speed */
  char pos_txt[3];     /* first char = B or T, second = L or R */
  int line_width;      /* Width of the line aroud the image */
} image;

typedef struct {
  int z;                      /* depth [1 - 3] */
  int maxImages;              /* max count of images in that layer */
  int imageCount;             /* count of images displayer in that layer */
  int lastImageCount;         /* previous count of images (for debug purpose) */
  image* *images;             /* images displayed on that layer */
  double next_insertion_time; /* minimum time for the next insertion in the layer */
} layer;

typedef struct {
  GLXContext *glx_context;

  layer layers[3];                /* pointers to the layers */

  double now;              /* current time in seconds */
  double img_last_change_time;    /* Last change of the displayed images */
  
  XFontStruct *xfont;             /* for printing image file names */
  GLuint font_dlist;

  int image_id;                   /* debugging id for image */

  int directions;                  /* general directions (see constants) */

  double next_direction_change_time; /* time to next change the direction */
  double next_insertion_time;     /* time to next insert an image to the screen */

} treadmill_state;

static treadmill_state *sss = NULL;

/* Command-line arguments */
static int cover_time_p;    /* Reference time to cover the full screen */
static int amplitude_p;     /* Amplitude percentage related to cover time */
static int img_count_p;     /* Nearly the max simutaneous displayed images */
static int stretch_min_p;  /* min stretch of images (percentage of screen dimensions) */
static int stretch_max_p;  /* max stretch of images */
static int directions_p;    /* allowed directions */
static int change_dir_p;    /* time before the image directions changes (if directions_p >= 5) */
static int line_width_p;    /* line width around pictures */
static Bool do_titles_p;    /* Display image titles. */
static Bool do_debug_p;     /* Be loud and do weird things. */ 
static Bool change_mode_p;  /* Whether the directions mode can change */
static int debug_p;         /* debug level */


static XrmOptionDescRec opts[] = {
  {"-cover_time",   ".coverTime",  XrmoptionSepArg, 0   },
  {"-amplitude",    ".amplitude",  XrmoptionSepArg, 0   },
  {"-img_count",    ".imgCount",   XrmoptionSepArg, 0   },
  {"-stretch_min",  ".stretchMin", XrmoptionSepArg, 0   },
  {"-stretch_max",  ".stretchMax", XrmoptionSepArg, 0   },
  {"-directions",   ".directions", XrmoptionSepArg, 0   },
  {"-change_dir",   ".changeDir",  XrmoptionSepArg, 0   },
  {"-line_width",   ".lineWidth",  XrmoptionSepArg, 0   },
  {"-titles",       ".titles",     XrmoptionNoArg,  "True"  },
  {"-debug",        ".debug",      XrmoptionNoArg,  "True"  },
  {"-debug_level",  ".debugLevel", XrmoptionSepArg, 0  },
};

static argtype vars[] = {
  { &cover_time_p,    "coverTime",    "CoverTime",   DEF_COVER_TIME,    t_Int},
  { &amplitude_p,     "amplitude",    "Amplitude",   DEF_AMPLITUDE,     t_Int},
  { &img_count_p,     "imgCount",     "ImgCount",    DEF_IMG_COUNT,     t_Int},
  { &stretch_min_p,   "stretchMin",   "StretchMin",  DEF_STRETCH_MIN,   t_Int},
  { &stretch_max_p,   "stretchMax",   "StretchMax",  DEF_STRETCH_MAX,   t_Int},
  { &directions_p,    "directions",   "Directions",  DEF_DIRECTIONS,    t_Int},
  { &change_dir_p,    "changeDir",    "ChangeDir",   DEF_CHANGE_DIR,    t_Int},
  { &line_width_p,    "lineWidth",    "LineWidth",   DEF_LINE_WIDTH,    t_Int},
  { &debug_p,         "debugLevel",   "DebugLevel",  DEF_DEBUG_LEVEL,   t_Int},
  { &do_debug_p,      "debug",        "Debug",       DEF_DEBUG,         t_Bool},
  { &do_titles_p,     "titles",       "Titles",      DEF_TITLES,        t_Bool},
};

ENTRYPOINT ModeSpecOpt treadmill_opts = {countof(opts), opts, countof(vars), vars, NULL};

static const char *
blurb (void)
{
# ifdef HAVE_COCOA
  return "treadmill";
# else
  static char buf[255];
  time_t now = time ((time_t *) 0);
  char *ct = (char *) ctime (&now);
  int n = strlen(progname);
  if (n > 100) n = 99;
  strncpy(buf, progname, n);
  buf[n++] = ':';
  buf[n++] = ' ';
  strncpy(buf+n, ct+11, 8);
  strcpy(buf+n+9, ": ");
  return buf;
# endif
}

/* returns an angle compatible with the current allowed directions */ 
static double get_dir_radians(ModeInfo *mi) 
{
  double ret;

  if (debug_p >= 3) fprintf(stderr, "%s : get_dir_radians\n", blurb());

  if (directions_p == ANY_DIR) 
  {
    ret = M_PI * (random() - RAND_MAX_2) / (double) RAND_MAX_2;

  } 
  else  
  {
    int dir;

    if (directions_p == MULTI_CARDINAL_DIR) 
    {
      dir = random() % 4 + 1;
    } 
    else 
    {
      treadmill_state *ss = &sss[MI_SCREEN(mi)];
      dir = ss->directions;
    }

    switch(dir) 
    {
      case LEFT2RIGHT : 
        ret = 0;
        break;
      case RIGHT2LEFT : 
        ret = M_PI;
        break;
      case TOP2BOTTOM : 
        ret = -M_PI / 2;
        break;
      default : 
        ret = M_PI / 2;
        break;
    }
  }
  return ret;
}

/* Returns the current time in seconds as a double.
 */
static double
double_time (void)
{
  struct timeval now;
# ifdef GETTIMEOFDAY_TWO_ARGS
  struct timezone tzp;
  gettimeofday(&now, &tzp);
# else
  gettimeofday(&now);
# endif

  return (now.tv_sec + ((double) now.tv_usec * 0.000001));
}

/* inserts an image in one of the displayed layers */
static Bool insert_image(ModeInfo *mi) 
{

  treadmill_state *ss = &sss[MI_SCREEN(mi)];
  int l;
  int def_layer;
  int rand;
  image *img;
  layer *target_layer;
  
  int randAmplitude;
  double delta_cover, speed, rad, absRad;

  if (debug_p >= 3) fprintf(stderr, "%s : insert_image\n", blurb());
  
  target_layer = NULL;
  img = NULL;
  rand = random() % 10;
  
  if (rand == 3) 
  {
    def_layer = 0;
  } 
  else if (rand < 3) 
  {
    def_layer = 1;
  } 
  else 
  {
    def_layer = 2;
  }

  for (l = 0; l < 3 && target_layer == NULL ; l++) 
  {
    int i = (l + def_layer) % 3;
    if (ss->layers[i].imageCount < ss->layers[i].maxImages
        && ss->layers[i].next_insertion_time < ss->now) 
    {
      int j;
      for (j = 0 ; j < ss->layers[i].maxImages && target_layer == NULL ; j++) 
      {
        img = ss->layers[i].images[j];
        if (img != NULL && img->loaded && ! img->used) 
        {
          target_layer = &(ss->layers[i]);
        }
      }
    } 
  }

  if (target_layer == NULL) 
  {
    return False;
  }

  if (debug_p >= 2) 
    fprintf(stderr, "%s : Initializing img %d to be displayed on layer %d \n",
            blurb(), img->id, target_layer->z);

  img->used = True;
  target_layer->imageCount++; 
  target_layer->next_insertion_time = 
        ss->now + cover_time_p / (float) target_layer->maxImages;

  randAmplitude = random() % 101;
  switch(target_layer->z) 
  {
    case 3 : 
      delta_cover = -0.6 - 0.4 * randAmplitude / 100.0;
      break;
    case 2 : 
      delta_cover = -0.6 + 0.9 * randAmplitude / 100.0;
      break;
    default : 
      delta_cover = 0.3 + 0.7 * randAmplitude / 100.0;
      break;
  }
  delta_cover *= amplitude_p / 100.0;
  speed = 2.0 / (cover_time_p * (1 - delta_cover));                             
  rad = get_dir_radians(mi);
     
  img->hspeed = speed * cos(rad);
  img->vspeed = speed * sin(rad);

  absRad = rad < 0 ? -rad : rad; 
  if (absRad < M_PI / 8.0 || absRad > 7.0 * M_PI / 8) 
  {
    /* start from the left or the right side */
    float x, h;

    x = 1 + img->pos.w;
    if (absRad < M_PI / 2) 
      img->pos.x = -x;
    else 
      img->pos.x = x;

    h = 2.0 - img->pos.h * 2.0;
    img->pos.y = h * random() / (double) RAND_MAX - h/2;
      
  } 
  else if (absRad > 3.0 * M_PI / 8 && absRad < 5.0 * M_PI / 8) 
  {
    /* starts from the top or the bottom side */
    float y, w;

    w = 2.0 - img->pos.w * 2.0;
    img->pos.x = w * random() / (double) RAND_MAX - w/2;

    y = 1 + img->pos.h;
    if (rad > 0) 
      img->pos.y = -y;
    else 
      img->pos.y = y;
  } 
  else 
  {
    /* start from a corner */
    double pos, l, x, y;

    l = 2.0 + img->pos.h + img->pos.w;
    pos = l * random() / (double) RAND_MAX;
    if (pos > 1.0 + img->pos.w) 
    {
      x = 1.0 + img->pos.w;
      y = pos - x;
    } 
    else 
    {
      x = pos;
      y = 1.0 + img->pos.h;
    }

    if (absRad < M_PI / 2) 
      img->pos.x = -x;
    else 
      img->pos.x = x;

    if (rad > 0) 
      img->pos.y = -y;
    else 
      img->pos.y = y;
  }

  img->pos_txt[0] = img->pos.y >= 0 ? 'B' : 'T';
  img->pos_txt[1] = img->pos.x >= 0 ? 'L' : 'R';
  img->pos_txt[2] = '\0';
    
  if (line_width_p >= 0) {
    img->line_width = line_width_p;
  } 
  else 
  {
    img->line_width = random() % (1 - line_width_p);
  } 

  if (debug_p >= 2) 
  {
    fprintf(stderr, "%s : Insertion image %d dans layer %d : pos=[%f, %f, %f, %f], speed=[%f, %f], angle=%f, speed=%f \n", 
            blurb(), img->id, target_layer->z, img->pos.x, img->pos.y, img->pos.w, img->pos.h, img->hspeed, img->vspeed, rad, speed);
  }

  return True;
}

static void image_loaded_cb (const char *filename, XRectangle *geom,
                             int image_width, int image_height,
                             int texture_width, int texture_height,
                             void *closure);


/* Allocate an image structure and start a file loading in the background.
 */
static void
alloc_images (ModeInfo *mi)
{
  int i;
  treadmill_state *ss = &sss[MI_SCREEN(mi)];
  int loading;

  if (debug_p >= 3) fprintf(stderr, "%s : alloc_images\n", blurb());

  loading = 0;
  for (i = 0; i < MAX_LAYERS ; i++) 
  {
    int j;
    for (j = 0 ; j < ss->layers[i].maxImages ; j++) 
    {
      image * img = ss->layers[i].images[j];
      if (img != NULL && img->loaded == False) 
      {
        loading++;
      }
    }
  }

  for (i = MAX_LAYERS - 1 ; i >= 0 && loading < BUF_IMAGE_LOADING_MAX; i--) 
  {
    int j;
    for (j = 0 ; j < ss->layers[i].maxImages && loading < BUF_IMAGE_LOADING_MAX ; j++) 
    {
      image *img = ss->layers[i].images[j];
    
      if (img == NULL) 
      {
        img = (image *) calloc (1, sizeof (*img));
 
        img->id = ++ss->image_id;
        img->loaded = False;
        img->used = False;
        img->useless = False;
        img->mi = mi;

        glGenTextures (1, &img->texid);
        if (img->texid <= 0) abort();

        switch(ss->layers[i].z) 
        {
          case 3 : 
            img->stretch = stretch_min_p * 2 * (1 + (random() % 30) / 100.0);
            break;
          case 2 : 
            img->stretch = (stretch_min_p + stretch_max_p) 
                            * (0.85 + (random() % 30) / 100.0) ;
            break;
          default : 
            img->stretch = stretch_max_p * 2 * (1 - (random() % 30) / 100.0);
            break;
        }

        load_texture_async (mi->xgwa.screen, mi->window, *ss->glx_context,
                            mi->xgwa.width * img->stretch / 100.0, 
                            mi->xgwa.height * img->stretch / 100.0, 
                            False, img->texid, image_loaded_cb, img);

        ss->layers[i].images[j] = img;
        loading++;

        if (debug_p) 
          fprintf (stderr, "%s: start loading img %2d for layer %d: \n",
                   blurb(), img->id, ss->layers[i].z);
      }
    }
  }
}


/* Callback that tells us that the texture has been loaded.
 */
static void
image_loaded_cb (const char *filename, XRectangle *geom,
                 int image_width, int image_height,
                 int texture_width, int texture_height,
                 void *closure)
{
  image *img = (image *) closure;
  ModeInfo *mi = img->mi;
  double ratio_image;
  double ratio_screen;
  double coefx, coefy;
  GLfloat texw, texh;

  if (debug_p >= 3) fprintf(stderr, "%s : image_loaded_cb\n", blurb());

  if (image_width == 0 || image_height == 0)
    exit (1);


  ratio_image = (double) geom->width / geom->height;
  ratio_screen = (double) image_width / image_height;

  if (debug_p >= 2) 
  {
    fprintf(stderr, "%s : >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n", blurb());
    fprintf(stderr, "%s : Image : %d\n", blurb(), img->id);
    fprintf(stderr, "%s : Demande : w=%d, h=%d\n", 
           blurb(), (int) (mi->xgwa.width * img->pos.w), (int) (mi->xgwa.height * img->pos.h));
    fprintf(stderr, "%s : geom : x=%d, y=%d, w=%d, h=%d, ratio=%f\n", blurb(), geom->x, geom->y, geom->width, geom->height,ratio_image);
    fprintf(stderr, "%s : image_width=%d, image_height=%d, ratio=%f\n", blurb(), image_width, image_height, ratio_screen);
    fprintf(stderr, "%s : texture_width=%d, texture_height=%d\n", blurb(), texture_width, texture_height);
    fprintf(stderr, "%s : <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n", blurb());
  }

  if (ratio_image >= ratio_screen) 
  {
    coefx = 0.5;
    coefy = ratio_screen / (2 * ratio_image);
  } 
  else 
  {
    coefy = 0.5;
    coefx = ratio_image / (2 * ratio_screen);
  }
  img->pos.w = img->stretch * coefx / 100.0;
  img->pos.h = img->stretch * coefy / 100.0;

  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  texw  = geom->width  / (GLfloat) texture_width;
  texh  = geom->height / (GLfloat) texture_height;
  img->tex_point_1.x = geom->x / (GLfloat) texture_width;
  img->tex_point_1.y = geom->y / (GLfloat) texture_height;
  img->tex_point_2.x = img->tex_point_1.x + texw;
  img->tex_point_2.y = img->tex_point_1.y + texh;

  img->w  = image_width;
  img->h  = image_height;
  img->geom = *geom;
  img->title = (filename ? strdup (filename) : 0);

  if (img->title)   /* strip filename to part after last /. */
  {
    int i;
    char *s = strrchr (img->title, '/');
    if (s) strcpy (img->title, s+1);
    /* Assumes filename is UTF-8 => convert to ASCII */
    for (i = strlen(img->title) - 2; i > 0; i--) {
      unsigned char c = img->title[i];
      if ((c & 0xE0) == 0xC0)
      {
        unsigned char c2 = (unsigned char) img->title[i+1];
        if ((c2 & 0xC0) == 0x80)
        {
          int j;
          img->title[i] = ((c & 0x1F) << 6) | (c2 & 0x3F) ;
          for (j = i + 1 ; j < strlen(img->title) ; j++) {
            img->title[j] = img->title[j + 1];
          }
        }
      }
    }
  }

  img->loaded = True;
  if (debug_p)
    fprintf (stderr, "%s: loaded img [id=%d, title=%s, w=%d, h=%d, geom=[%d, %d, %d, %d], texid=%d, tex point 1=[%f, %f], tex point 2=[%f, %f], posWH/2=[%f, %f], stretch=%d]\n",
             blurb(), img->id, 
             img->title ? img->title : "(nil)", 
             img->w, img->h, 
             img->geom.x, img->geom.y, img->geom.width, img->geom.height, 
             img->texid, 
             img->tex_point_1.x, img->tex_point_1.y, 
             img->tex_point_2.x, img->tex_point_2.y, 
             img->pos.w, img->pos.h,
             img->stretch);
}

/* Free the image and texture, after nobody is referencing it.
 */
static void
destroy_images (ModeInfo *mi)
{
  treadmill_state *ss = &sss[MI_SCREEN(mi)];
  int i;

  if (debug_p >= 3) fprintf(stderr, "%s : destroy_images\n", blurb());

  for (i = 0; i < MAX_LAYERS ; i++) 
  {
    int j;
    for (j = 0; j < ss->layers[i].maxImages ; j++) 
    {
      image *img = ss->layers[i].images[j];

      if (img != NULL && img->useless) 
      {
    
        if (img->texid <= 0) abort();

        if (debug_p) 
          fprintf (stderr, "%s: destroying img %2d from layer %d : \"%s\"\n",
                   blurb(), img->id, i, (img->title ? img->title : "(nil)"));

        if (img->title) free (img->title);
        glDeleteTextures (1, &img->texid);
        free (img);
        ss->layers[i].images[j] = NULL;
      }
    }
  }
}

/* Draw the given sprite at the phase of its animation dictated by
   its creation time compared to the current wall clock.
 */
static void
draw_image (ModeInfo *mi, image *img)
{
  GLfloat xmin, ymin, xmax, ymax;

  if (debug_p >= 3) fprintf(stderr, "%s : draw_image\n", blurb());
 
  if (! img->loaded) abort();

  glPushMatrix();

  xmin = img->pos.x - img->pos.w;
  ymin = img->pos.y - img->pos.h;
  xmax = img->pos.x + img->pos.w;
  ymax = img->pos.y + img->pos.h;
  
  glBindTexture (GL_TEXTURE_2D, img->texid);
  glColor4f (1, 1, 1, 1);
  glNormal3f (0, 0, 1);

  glBegin (GL_QUADS);
  glTexCoord2f (img->tex_point_1.x, img->tex_point_2.y); glVertex3f ( xmin , ymin , 0);
  glTexCoord2f (img->tex_point_2.x, img->tex_point_2.y); glVertex3f ( xmax , ymin , 0);
  glTexCoord2f (img->tex_point_2.x, img->tex_point_1.y); glVertex3f ( xmax , ymax , 0);
  glTexCoord2f (img->tex_point_1.x, img->tex_point_1.y); glVertex3f ( xmin , ymax , 0);
  glEnd();
  
  if (img->line_width) 
  {
    glLineWidth(img->line_width);

    glDisable (GL_TEXTURE_2D);
    glBegin (GL_LINE_LOOP);
    glVertex3f ( xmin , ymin , 0);
    glVertex3f ( xmax , ymin , 0);
    glVertex3f ( xmax , ymax , 0);
    glVertex3f ( xmin , ymax , 0);
    glEnd();
    glEnable (GL_TEXTURE_2D);
  }

  if (do_titles_p && img->title && *img->title) 
  {
    int x,y,w,h;
    treadmill_state *ss = &sss[MI_SCREEN(mi)];

    if (img->pos_txt[1] == 'L') 
    {
      x = (1 + xmin) / 2 * mi->xgwa.width;
      h = 0;
    } 
    else 
    {
      x = (1 + xmax) / 2 * mi->xgwa.width;
      w = string_width(ss->xfont, img->title, &h);
      x -= w;
    }
    if (img->pos_txt[0] == 'T') 
    {
      y = (1 + ymax) / 2 * mi->xgwa.height + 4 + img->line_width / 2;
      if (h == 0) 
      {
        w = string_width(ss->xfont, img->title, &h);
      }
      y += h;
    }
    else 
    {
      y = (1 + ymin) / 2 * mi->xgwa.height - 2 - img->line_width / 2;
    }

    glColor4f (1, 1, 1, 1);
    print_gl_string (mi->dpy, ss->xfont, ss->font_dlist,
                     mi->xgwa.width, mi->xgwa.height, x, y,
                     img->title, False);
  }

  glPopMatrix();
}

/* Move the images of a layer and draw them.
 */
static void
move_and_draw_layer (ModeInfo *mi, layer *aLayer) 
{
  int i;

  float delta;
  treadmill_state *ss = &sss[MI_SCREEN(mi)];

  if (debug_p >= 3) fprintf(stderr, "%s : move_and_draw_layer\n", blurb());
 
  delta = ss->now - ss->img_last_change_time;

  for (i = 0; i < aLayer->maxImages ; i++) 
  {
    image *img = aLayer->images[i];
    if (img != NULL && img->used) 
    {
      img->pos.x += img->hspeed * delta; 
      img->pos.y += img->vspeed * delta;

      if (img->pos.x > 1 + img->pos.w 
          || img->pos.x < -1 - img->pos.w
          || img->pos.y < -1 - img->pos.h
          || img->pos.y > 1 + img->pos.h) 
      {
        img->useless = True;
        aLayer->imageCount--;
      }

      if (! img->useless) 
      {
        draw_image(mi, img);
      } 
    }
  }
}


ENTRYPOINT void
reshape_treadmill (ModeInfo *mi, int width, int height)
{
  int i;
  double ratio_image, ratio_screen;

  treadmill_state *ss = &sss[MI_SCREEN(mi)];

  if (debug_p >= 3) fprintf(stderr, "%s : reshape_treadmill\n", blurb());
 
  glViewport (0, 0, width, height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (height > 0) 
  {
    ratio_screen = (double) width / height;

    for (i = 0; i < MAX_LAYERS ; i++) 
    {
      int j;
      for (j = 0; j < ss->layers[i].maxImages ; j++) 
      {
        image *img = ss->layers[i].images[j];

        if (img != NULL && img->loaded == True) 
        {
          double coefx, coefy;

          ratio_image = (double) img->geom.width / img->geom.height;
          if (ratio_image >= ratio_screen) 
          {
            coefx = 0.5;
            coefy = ratio_screen / (2 * ratio_image);
          } 
          else 
          {
            coefy = 0.5;
            coefx = ratio_image / (2 * ratio_screen);
          }
          img->pos.w = img->stretch * coefx / 100.0;
          img->pos.h = img->stretch * coefy / 100.0;
        }
      }
    }
  }
}


ENTRYPOINT Bool
treadmill_handle_event (ModeInfo *mi, XEvent *event)
{
  return False;
}


/* Kludge to add "-v" to invocation of "xscreensaver-getimage" in -debug mode
 */
static void
hack_resources (void)
{
#if 0
  char *res = "desktopGrabber";
  char *val = get_string_resource (res, "DesktopGrabber");
  char buf1[255];
  char buf2[255];
  XrmValue value;
  sprintf (buf1, "%.100s.%.100s", progclass, res);
  sprintf (buf2, "%.200s -v", val);
  value.addr = buf2;
  value.size = strlen(buf2);
  XrmPutResource (&db, buf1, "String", &value);
#endif
}


ENTRYPOINT void
init_treadmill (ModeInfo *mi)
{
  int screen = MI_SCREEN(mi);
  treadmill_state *ss;
  int i,j;

  if (debug_p >= 3) fprintf(stderr, "%s : init_treadmill\n", blurb());
 
  if (sss == NULL) 
  {
    if ((sss = (treadmill_state *)
         calloc (MI_NUM_SCREENS(mi), sizeof(treadmill_state))) == NULL)
    {
      return;
    }
  }
  ss = &sss[screen];

  if (do_debug_p && ! debug_p) 
  {
    debug_p = 1;
  }
  if (cover_time_p < 1 || cover_time_p > 300) 
  {
    cover_time_p = atoi(DEF_COVER_TIME);
  }
  if (amplitude_p < 0 || amplitude_p > 100) 
  {
    amplitude_p = atoi(DEF_AMPLITUDE);
  }
  if (img_count_p < 3 || img_count_p > 50) 
  {
    img_count_p = atoi(DEF_IMG_COUNT);
  }
  if (stretch_min_p < 1 || stretch_min_p > 99) 
  {
    stretch_min_p = atoi(DEF_STRETCH_MIN);
  }
  if (stretch_max_p < 1 || stretch_max_p > 99) 
  {
    stretch_max_p = atoi(DEF_STRETCH_MAX);
  }
  if (stretch_min_p > stretch_max_p) 
  {
    stretch_min_p = stretch_max_p;
  }
  if (directions_p < 1 || directions_p > 8) 
  {
    directions_p = atoi(DEF_DIRECTIONS);
  } 
  if (change_dir_p < 0) 
  {
    change_dir_p = atoi(DEF_CHANGE_DIR);
  }
  if (directions_p <= 4) 
  {
    change_dir_p = 0;
  }
  if (directions_p == 8) 
  {
    change_mode_p = True;
    directions_p = random() % 6 + 1;
    if (directions_p > 4) {
      directions_p++;
    }
  }   
  else 
  {
    change_mode_p = False;
  }

  if (debug_p) 
  {
    fprintf(stderr, "%s : Starting : cover_time=%d, amplitude=%d%%, img_count=%d, stretch=[%d%%, %d%%], direction=%d, change_dir=%d, debug_level=%d\n",
            blurb(), cover_time_p, amplitude_p, img_count_p, 
            stretch_min_p, stretch_max_p, directions_p, change_dir_p,
            debug_p);
  }

  ss->now = double_time();
  ss->img_last_change_time = ss->now;

  for (i = 0; i < 3 ; i++) 
  {
    ss->layers[i].z = i + 1;
    ss->layers[i].imageCount = 0;
    switch(i) 
    {
      case 0 : 
        if (img_count_p > 10 && stretch_max_p <= 50) 
        {
          ss->layers[0].maxImages = 2;
        } 
        else 
        {
          ss->layers[0].maxImages = 1;
        }
        break;
      case 1 : 
        ss->layers[1].maxImages = 1 + 0.25 * img_count_p;
        break;
      default : 
        ss->layers[i].maxImages = img_count_p - ss->layers[1].maxImages  
                                              - ss->layers[0].maxImages;
        break;
    }
    ss->layers[i].images = (image **) calloc(ss->layers[i].maxImages, sizeof(image *));
    for (j = 0 ; j < ss->layers[i].maxImages ; j++) 
    {
      ss->layers[i].images[j] = NULL;
    }
  }

  if ((ss->glx_context = init_GL(mi)) != NULL) 
  {
    reshape_treadmill (mi, MI_WIDTH(mi), MI_HEIGHT(mi));
  } 
  else 
  {
    MI_CLEARWINDOW(mi);
  }

  glDisable (GL_LIGHTING);
  glDisable (GL_DEPTH_TEST);
  glDepthMask (GL_FALSE);
  glEnable (GL_CULL_FACE);
  glCullFace (GL_BACK);

  glEnable (GL_TEXTURE_2D);
  glShadeModel (GL_SMOOTH);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  load_font (mi->dpy, "titleFont", &ss->xfont, &ss->font_dlist);

  if (debug_p)
    hack_resources();

  if (directions_p <= 4) 
  {
    ss->directions = directions_p;
  } 
  else if (directions_p == 5) 
  {
    ss->directions = random() % 4 + 1;
  } 
  else 
  {
    ss->directions = 0;
  }

  ss->next_direction_change_time = ss->now + change_dir_p;
  ss->next_insertion_time = 0;
  ss->img_last_change_time = ss->now;
}


ENTRYPOINT void
draw_treadmill (ModeInfo *mi)
{
  treadmill_state *ss = &sss[MI_SCREEN(mi)];
  int i,l;

  if (debug_p >= 3) fprintf(stderr, "%s : draw_treadmill\n", blurb());
 
  if (!ss->glx_context)
  {
    return;
  }

  glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(ss->glx_context));

  destroy_images(mi);
  alloc_images(mi);

  ss->now = double_time();

  /* Change direction */
  if (change_dir_p > 0 && ss->now > ss->next_direction_change_time) 
  {
    if (change_mode_p) 
    {
      /* Change the mode... */
      directions_p = random() % 6 + 1;
      if (directions_p > 4) {
        directions_p++;
      }

      if (directions_p <= 4) 
      {
        ss->directions = directions_p;
      } 
      else if (directions_p == 5) 
      {
        ss->directions = random() % 4 + 1;
      } 
      else 
      {
        ss->directions = 0;
      }
    }

    if (ss->directions > 0) 
    {
      int newDirection;

      newDirection = ss->directions;
      while (newDirection == ss->directions) 
      { 
        newDirection = random() %3 + 1;
      }
      ss->directions = newDirection;
    }

    for (i = 0; i < MAX_LAYERS; i++) 
    {
      int k;

      for (k = 0; k < ss->layers[i].maxImages ; k++) 
      {
        image * img;
        img = ss->layers[i].images[k];
        if (img != NULL && img->used) 
        {
          double speed, rad;
          speed = sqrt(img->hspeed * img->hspeed + img->vspeed * img->vspeed);
          rad = get_dir_radians(mi);
          img->hspeed = speed * cos(rad);
          img->vspeed = speed * sin(rad);
        }
      }
    }
    ss->next_direction_change_time = ss->now + change_dir_p;
  }

  /* Choose the new image to be displayed */
  if (ss->now > ss->next_insertion_time) 
  {
    if (insert_image(mi)) 
    {
      ss->next_insertion_time = ss->now + (0.7 + 0.6 * random() / (double) RAND_MAX) * cover_time_p / img_count_p;
    } 
    else 
    {
      ss->next_insertion_time = ss->now + (0.15 * random() / (double) RAND_MAX) * cover_time_p / img_count_p;
    }
  }
   
  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT);

  /* Move and draw the images */
  for (l = MAX_LAYERS - 1; l >= 0 ; l--) 
  {
    layer *theLayer = &ss->layers[l];

    if (theLayer != NULL) 
    {
      if (debug_p >= 2 && theLayer->imageCount != theLayer->lastImageCount) 
      {
        fprintf(stderr, "%s : drawing layer %d : %d/%d images\n", 
                blurb(), theLayer->z, theLayer->imageCount, theLayer->maxImages);
        theLayer->lastImageCount = theLayer->imageCount;
      }

      move_and_draw_layer(mi, theLayer);
    }
  }

  if (mi->fps_p) do_fps(mi);

  glFinish();
  glXSwapBuffers (MI_DISPLAY (mi), MI_WINDOW(mi));

  destroy_images(mi);

  ss->img_last_change_time = ss->now;
}

XSCREENSAVER_MODULE_2 ("Treadmill", gltreadmill, treadmill)

#endif /* USE_GL */
