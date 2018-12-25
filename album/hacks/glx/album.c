/* album, 2010 C.Gallioz
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
 * Displays some images as in a photo album.
 * This was initially written by modifying glslideshow (Copyright (c) 2003-2008 Jamie Zawinski <jwz@jwz.org>).
 */

#define DEFAULTS  "*delay:           30000                \n" \
                  "*showFPS:         False                \n" \
                  "*fpsSolid:        True                 \n" \
                  "*useSHM:          True                 \n" \
                  "*titleFont:       -*-helvetica-medium-r-normal-*-14-*\n" \
                  "*desktopGrabber:  xscreensaver-getimage -no-desktop %s\n" \
                  "*grabDesktopImages:   False \n" \
                  "*chooseRandomImages:  True  \n"

# define free_album 0
# define release_album 0
# include "xlockmore.h"

#undef countof
#define countof(x) (sizeof((x))/sizeof((*x)))

#ifdef USE_GL

# define DEF_IMAGE_DURATION "15"
# define DEF_TITLES         "False"
# define DEF_DEBUG          "False"

#include "grab-ximage.h"
#include "texfont.h"

#define BUF_IMAGE_SIZE 8
#define BUF_IMAGE_LOADING_MAX 2

#define NB_POSITIONS 8

typedef struct {
  double x, y, w, h;
  char *txt;
} rect;

typedef struct {
  char *name;
  int count;
  rect *photos;
} position;

typedef struct {
  ModeInfo *mi;
  int id;			   /* unique number for debugging */
  char *title;			   /* the filename of this image */
  int w, h;			   /* size in pixels of the image */
  int tw, th;			   /* size in pixels of the texture */
  double coefx, coefy;             /* coefs for width and height */
  XRectangle geom;		   /* where in the image the bits are */
  Bool loaded_p;		   /* whether the image has finished loading */
  Bool used_p;			   /* whether the image has yet appeared
                                      on screen */
  GLuint texid;			   /* which texture contains the image */
} image;


typedef struct {
  GLXContext *glx_context;
  image *images[BUF_IMAGE_SIZE];/* pointers to the images */
  image *displayedImg[10];      /* pointers to the displayed images */
  int curPos;                   /* Index of the current positions */
  int nextPos;                  /* index of the next positions */

  double now;			/* current time in seconds */
  double img_last_change_time;  /* Last change of the displayed images */
  double dawn_of_time;		/* when the program launched */
  double image_load_time;	/* time when we last loaded a new image */
  double prev_frame_time;	/* time when we last drew a frame */

  
  Bool await_for_images_p;
  Bool redisplay_needed_p;	/* Sometimes we can get away with not
                                   re-painting.  Tick this if a redisplay
                                   is required. */
  Bool change_now_p;		/* Set when the user clicks to ask for a new
                                   image right now. */

  GLfloat fps;                  /* approximate frame rate we're achieving */
  GLfloat theoretical_fps;      /* maximum frame rate that might be possible */
  Bool checked_fps_p;		/* Whether we have checked for a low
                                   frame rate. */

  texture_font_data *font_data;	/* for printing image file names */

  int image_id;                 /* debugging id for image */

  double time_elapsed;
  int frames_elapsed;

} album_state;

static album_state *sss = NULL;

static position *positions = NULL;

/* Command-line arguments
 */
static int image_seconds;   /* How many seconds until loading a new image. */
static Bool do_titles;	    /* Display image titles. */
static Bool debug_p;	    /* Be loud and do weird things. */


static XrmOptionDescRec opts[] = {
  {"-duration",     ".imageDuration", XrmoptionSepArg, 0   },
  {"-titles",       ".titles",        XrmoptionNoArg, "True"  },
  {"-debug",        ".debug",         XrmoptionNoArg, "True"  },
};

static argtype vars[] = {
  { &image_seconds, "imageDuration","ImageDuration",DEF_IMAGE_DURATION, t_Int},
  { &debug_p,       "debug",        "Debug",        DEF_DEBUG,         t_Bool},
  { &do_titles,     "titles",       "Titles",       DEF_TITLES,        t_Bool},
};

ENTRYPOINT ModeSpecOpt album_opts = {countof(opts), opts, countof(vars), vars, NULL};


static const char *
blurb (void)
{
# ifdef HAVE_JWXYZ
  return "album";
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

static char *
dump_img_props(image *img) 
{
  static char buf[1000];
  sprintf(buf, "id=%d; title=%s; w=%d; h=%d; tw=%d; th=%d; geom=(%d, %d, %d, %d); coefx=%f; coefy=%f; loaded=%s; used=%s; texid=%d", 
          img->id, img->title, img->w, img->h, img->tw, img->th, 
          img->geom.x, img->geom.y, img->geom.width, img->geom.height, 
          img->coefx, img->coefy,
          img->loaded_p?"True":"False", img->used_p?"True":"False", img->texid);

  return buf;
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
  album_state *ss = &sss[MI_SCREEN(mi)];
  int loading;

  loading = 0;
  for (i = 0; i < BUF_IMAGE_SIZE ; i++) 
  {
    if (ss->images[i] != 0 && ss->images[i]->loaded_p == False) {
      loading++;
    }
  }

  for (i = 0; i < BUF_IMAGE_SIZE && loading < BUF_IMAGE_LOADING_MAX; i++) 
    {
      if (ss->images[i] == 0) {
  
      image *img = (image *) calloc (1, sizeof (*img));
 
      img->id = ++ss->image_id;
      img->loaded_p = False;
      img->used_p = False;
      img->mi = mi;

      glGenTextures (1, &img->texid);
      if (img->texid <= 0) abort();

      ss->image_load_time = ss->now;

      load_texture_async (mi->xgwa.screen, mi->window, *ss->glx_context,
                          0, 0, False, img->texid, image_loaded_cb, img);

      ss->images[i] = img;
      loading++;

      if (debug_p) 
        fprintf (stderr, "%s: start loading img %2d: \n",
                 blurb(), img->id);
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
  double ratio_ecran;
  /* album_state *ss = &sss[MI_SCREEN(mi)]; */

  if (image_width == 0 || image_height == 0)
    exit (1);

  ratio_image = (double) geom->width / geom->height;
  ratio_ecran = (double) image_width / image_height;
  if (ratio_image >= ratio_ecran) 
  {
    img->coefx = 1;
    img->coefy = ratio_ecran / ratio_image;
  } 
  else 
  {
    img->coefy = 1;
    img->coefx = ratio_image / ratio_ecran;
  }

  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  img->w  = image_width;
  img->h  = image_height;
  img->tw = texture_width;
  img->th = texture_height;
  img->geom = *geom;
  img->title = (filename ? strdup (filename) : 0);

  /* If the image's width doesn't come back as the width of the screen,
     then the image must have been scaled down (due to insufficient
     texture memory.)  Scale up the coordinates to stretch the image
     to fill the window.
   */
  if (img->w != MI_WIDTH(mi))
    {
      double scale = (double) MI_WIDTH(mi) / img->w;
      img->w  *= scale;
      img->h  *= scale;
      img->tw *= scale;
      img->th *= scale;
      img->geom.x      *= scale;
      img->geom.y      *= scale;
      img->geom.width  *= scale;
      img->geom.height *= scale;
    }

  if (img->title)   /* strip filename to part after last /. */
  {
    char *s = strrchr (img->title, '/');
    if (s) strcpy (img->title, s+1);
  }

  img->loaded_p = True;
  if (debug_p)
    fprintf (stderr, "%s: loaded img [%s]\n",
             blurb(), dump_img_props(img));
}



/* Free the image and texture, after nobody is referencing it.
 */
static void
destroy_images (ModeInfo *mi)
{
  album_state *ss = &sss[MI_SCREEN(mi)];
  int i;

  for (i = 0; i < BUF_IMAGE_SIZE ; i++) 
  {
    image *img = ss->images[i];
    if (img != 0 && img->used_p == True) {
    
      if (img->texid <= 0) abort();

      if (debug_p) 
        fprintf (stderr, "%s: destroying img %2d: \"%s\"\n",
                 blurb(), img->id, (img->title ? img->title : "(null)"));

      if (img->title) free (img->title);
      glDeleteTextures (1, &img->texid);
      free (img);
      ss->images[i] = 0;
    }
  }
}

/* Draw the given sprite at the phase of its animation dictated by
   its creation time compared to the current wall clock.
 */
static void
draw_image (ModeInfo *mi, image *img, rect pos)
{
  GLfloat texw, texh, texx1, texx2, texy1, texy2, xmin, ymin, xmax, ymax;
  double deltax, deltay;
 
  album_state *ss = &sss[MI_SCREEN(mi)];

  if (! img->loaded_p) abort();

  glPushMatrix();

  texw  = img->geom.width  / (GLfloat) img->tw;
  texh  = img->geom.height / (GLfloat) img->th;
  texx1 = img->geom.x / (GLfloat) img->tw;
  texy1 = img->geom.y / (GLfloat) img->th;
  texx2 = texx1 + texw;
  texy2 = texy1 + texh;

  deltax = pos.w * img->coefx / 2;
  deltay = pos.h * img->coefy / 2;
  xmin = pos.x - deltax;
  ymin = pos.y - deltay;
  xmax = pos.x + deltax;
  ymax = pos.y + deltay;
  
  if (debug_p) {
    fprintf (stderr, "%s: dessin de l'image %d : min, max = %f, %f, %f, %f\n",
             blurb(), img->id, xmin, ymin, xmax, ymax);
  } 

  glBindTexture (GL_TEXTURE_2D, img->texid);
  glColor4f (1, 1, 1, 1);
  glNormal3f (0, 0, 1);

  glBegin (GL_QUADS);
  glTexCoord2f (texx1, texy2); glVertex3f ( xmin , ymin , 0);
  glTexCoord2f (texx2, texy2); glVertex3f ( xmax , ymin , 0);
  glTexCoord2f (texx2, texy1); glVertex3f ( xmax , ymax , 0);
  glTexCoord2f (texx1, texy1); glVertex3f ( xmin , ymax , 0);
  glEnd();
  
  glDisable (GL_TEXTURE_2D);
  glBegin (GL_LINE_LOOP);
  glVertex3f ( xmin , ymin , 0);
  glVertex3f ( xmax , ymin , 0);
  glVertex3f ( xmax , ymax , 0);
  glVertex3f ( xmin , ymax , 0);
  glEnd();
  glEnable (GL_TEXTURE_2D);


  if (do_titles && img->title && *img->title)
  {
    int x,y;
	  int w, h = 0;
    double coef = 1.3;

    
/*
	  if (pos.txt[1] == 'L') 
    {
      x = (1 + xmin) / 2 * mi->xgwa.width + 3;
    } else {
          x = (1 + xmax) / 2 * mi->xgwa.width - 3;
          w = string_width(ss->xfont, img->title, &h);
          x -= w;
    }
	  if (pos.txt[0] == 'T') 
    {
          y = (1 + ymax) / 2 * mi->xgwa.height - 3;
    } else {
          y = (1 + ymin) / 2 * mi->xgwa.height + 6;
          if (h == 0) {
            w = string_width(ss->xfont, img->title, &h);
          }
          y += h;
    }
*/
    glPushMatrix(); 
    glLoadIdentity();
    glOrtho(0, coef * MI_WIDTH(mi), 0, coef * MI_HEIGHT(mi), -1, 1);

    glTranslatef ((xmin * MI_WIDTH(mi) + MI_WIDTH(mi)) * coef / 2, 
                  (ymin * MI_HEIGHT(mi) + MI_HEIGHT(mi)) * coef / 2,
                  0);
    glEnable (GL_TEXTURE_2D);
    glDisable (GL_DEPTH_TEST);

    glColor3f (0.3, 0.3, 0.3);
    print_texture_string (ss->font_data, img->title);

    glTranslatef (2, 2, 0);
    glColor3f (1, 1, 1);
    print_texture_string (ss->font_data, img->title);

    glEnable (GL_DEPTH_TEST);  
    glPopMatrix(); 
  }

  glPopMatrix();
}

ENTRYPOINT void
reshape_album (ModeInfo *mi, int width, int height)
{
  int i;
  double ratio_image, ratio_ecran;

  if (debug_p >= 1) fprintf(stderr, "%s : reshape_album %dx%d\n", blurb(), width, height);

  album_state *ss = &sss[MI_SCREEN(mi)];
  glViewport (0, 0, width, height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (height > 0) {
    ratio_ecran = (double) width / height;

    for (i = 0; i < BUF_IMAGE_SIZE; i++) {
      if (ss->images[i] != 0 && ss->images[i]->loaded_p == True) {
        ss->images[i]->w = width;
        ss->images[i]->w = height;

        ratio_image = (double) ss->images[i]->geom.width / ss->images[i]->geom.height;
        if (ratio_image >= ratio_ecran) 
        {
          ss->images[i]->coefx = 1;
          ss->images[i]->coefy = ratio_ecran / ratio_image;
        } 
        else 
        {
          ss->images[i]->coefy = 1;
          ss->images[i]->coefx = ratio_image / ratio_ecran;
        }
      }
    }
  }

  ss->redisplay_needed_p = True;
}


ENTRYPOINT Bool
album_handle_event (ModeInfo *mi, XEvent *event)
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
init_album (ModeInfo *mi)
{
  int screen = MI_SCREEN(mi);
  album_state *ss;
  int i;

  if (sss == NULL) {
    if ((sss = (album_state *)
         calloc (MI_NUM_SCREENS(mi), sizeof(album_state))) == NULL)
    {
      return;
    }
  }
  ss = &sss[screen];

  for (i = 0; i < BUF_IMAGE_SIZE ; i++) 
  {
    ss->images[i] = 0;
  }

  if ((ss->glx_context = init_GL(mi)) != NULL) {
    reshape_album (mi, MI_WIDTH(mi), MI_HEIGHT(mi));
  } else {
    MI_CLEARWINDOW(mi);
  }

  if (debug_p)
    fprintf (stderr, "%s: img: change images every %d seconds\n",
             blurb(),image_seconds);

  glDisable (GL_LIGHTING);
  glDisable (GL_DEPTH_TEST);
  glDepthMask (GL_FALSE);
  glEnable (GL_CULL_FACE);
  glCullFace (GL_BACK);

  glEnable (GL_TEXTURE_2D);
  glShadeModel (GL_SMOOTH);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glLineWidth (3);

  ss->font_data = load_texture_font (mi->dpy, "titleFont");

  if (debug_p)
    hack_resources();

  ss->now = double_time();
  ss->dawn_of_time = ss->now;
  ss->prev_frame_time = ss->now;
  ss->await_for_images_p = True;
  ss->img_last_change_time = 0;

  ss->nextPos = random() % NB_POSITIONS;

  positions = calloc(NB_POSITIONS, sizeof(position));

  positions[0].name = "2 photos, up-left, bottom-right";
  positions[0].count = 2;
  positions[0].photos = calloc(positions[0].count, sizeof(rect));
  positions[0].photos[0].x = -0.40;
  positions[0].photos[0].y = 0.40;
  positions[0].photos[0].w = 1.15;
  positions[0].photos[0].h = 1.15;
  positions[0].photos[0].txt = "TL";
  positions[0].photos[1].x = 0.40;
  positions[0].photos[1].y = -0.40;
  positions[0].photos[1].w = 1.15;
  positions[0].photos[1].h = 1.15;
  positions[0].photos[1].txt = "BR";

  positions[1].name = "3 photos, large in center, small up-right and bottom-left";
  positions[1].count = 3;
  positions[1].photos = calloc(positions[1].count, sizeof(rect));
  positions[1].photos[0].x = 0;
  positions[1].photos[0].y = 0;
  positions[1].photos[0].w = 1.4;
  positions[1].photos[0].h = 1.4;
  positions[1].photos[0].txt = "TL";
  positions[1].photos[1].x = -0.65;
  positions[1].photos[1].y = -0.65;
  positions[1].photos[1].w = 0.55;
  positions[1].photos[1].h = 0.55;
  positions[1].photos[1].txt = "TR";
  positions[1].photos[2].x = 0.65;
  positions[1].photos[2].y = 0.65;
  positions[1].photos[2].w = 0.55;
  positions[1].photos[2].h = 0.55;
  positions[1].photos[2].txt = "BL";

  positions[2].name = "4 photos, in each corner";
  positions[2].count = 4;
  positions[2].photos = calloc(positions[2].count, sizeof(rect));
  positions[2].photos[0].x = -0.5;
  positions[2].photos[0].y = 0.5;
  positions[2].photos[0].w = 0.96;
  positions[2].photos[0].h = 0.96;
  positions[2].photos[0].txt = "TL";
  positions[2].photos[1].x = 0.5;
  positions[2].photos[1].y = 0.5;
  positions[2].photos[1].w = 0.96;
  positions[2].photos[1].h = 0.96;
  positions[2].photos[1].txt = "TR";
  positions[2].photos[2].x = -0.5;
  positions[2].photos[2].y = -0.5;
  positions[2].photos[2].w = 0.96;
  positions[2].photos[2].h = 0.96;
  positions[2].photos[2].txt = "BL";
  positions[2].photos[3].x = 0.5;
  positions[2].photos[3].y = -0.5;
  positions[2].photos[3].w = 0.96;
  positions[2].photos[3].h = 0.96;
  positions[2].photos[3].txt = "BR";

  positions[3].name = "2 photos, up-right, bottom-left";
  positions[3].count = 2;
  positions[3].photos = calloc(positions[3].count, sizeof(rect));
  positions[3].photos[0].x = -0.40;
  positions[3].photos[0].y = -0.40;
  positions[3].photos[0].w = 1.15;
  positions[3].photos[0].h = 1.15;
  positions[3].photos[0].txt = "BL";
  positions[3].photos[1].x = 0.40;
  positions[3].photos[1].y = 0.40;
  positions[3].photos[1].w = 1.15;
  positions[3].photos[1].h = 1.15;
  positions[3].photos[1].txt = "TR";

  positions[4].name = "3 photos, large in center, small up-left and bottom-right";
  positions[4].count = 3;
  positions[4].photos = calloc(positions[4].count, sizeof(rect));
  positions[4].photos[0].x = 0;
  positions[4].photos[0].y = 0;
  positions[4].photos[0].w = 1.4;
  positions[4].photos[0].h = 1.4;
  positions[4].photos[0].txt = "TR";
  positions[4].photos[1].x = -0.65;
  positions[4].photos[1].y = 0.65;
  positions[4].photos[1].w = 0.55;
  positions[4].photos[1].h = 0.55;
  positions[4].photos[1].txt = "TL";
  positions[4].photos[2].x = 0.65;
  positions[4].photos[2].y = -0.65;
  positions[4].photos[2].w = 0.55;
  positions[4].photos[2].h = 0.55;
  positions[4].photos[2].txt = "BR";

  positions[5].name = "4 photos, a large on right, 3 small on left";
  positions[5].count = 4;
  positions[5].photos = calloc(positions[5].count, sizeof(rect));
  positions[5].photos[0].x = 0.38;
  positions[5].photos[0].y = 0;
  positions[5].photos[0].w = 1.2;
  positions[5].photos[0].h = 1.2;
  positions[5].photos[0].txt = "BR";
  positions[5].photos[1].x = -0.6;
  positions[5].photos[1].y = 0.65;
  positions[5].photos[1].w = 0.6;
  positions[5].photos[1].h = 0.6;
  positions[5].photos[1].txt = "TL";
  positions[5].photos[2].x = -0.45;
  positions[5].photos[2].y = 0;
  positions[5].photos[2].w = 0.6;
  positions[5].photos[2].h = 0.6;
  positions[5].photos[2].txt = "BL";
  positions[5].photos[3].x = -0.6;
  positions[5].photos[3].y = -0.65;
  positions[5].photos[3].w = 0.6;
  positions[5].photos[3].h = 0.6;
  positions[5].photos[3].txt = "BL";

  positions[6].name = "1 large photo, centered";
  positions[6].count=1;
  positions[6].photos = calloc(positions[6].count, sizeof(rect));
  positions[6].photos[0].x = 0;
  positions[6].photos[0].y = 0;
  positions[6].photos[0].w = 1.95;
  positions[6].photos[0].h = 1.95;
  positions[6].photos[0].txt = "BL";

  positions[7].name = "4 photos, a large on left, 3 small on right";
  positions[7].count = 4;
  positions[7].photos = calloc(positions[7].count, sizeof(rect));
  positions[7].photos[0].x = -0.38;
  positions[7].photos[0].y = 0;
  positions[7].photos[0].w = 1.2;
  positions[7].photos[0].h = 1.2;
  positions[7].photos[0].txt = "BL";
  positions[7].photos[1].x = 0.6;
  positions[7].photos[1].y = 0.65;
  positions[7].photos[1].w = 0.6;
  positions[7].photos[1].h = 0.6;
  positions[7].photos[1].txt = "TR";
  positions[7].photos[2].x = 0.45;
  positions[7].photos[2].y = 0;
  positions[7].photos[2].w = 0.6;
  positions[7].photos[2].h = 0.6;
  positions[7].photos[2].txt = "BR";
  positions[7].photos[3].x = 0.6;
  positions[7].photos[3].y = -0.65;
  positions[7].photos[3].w = 0.6;
  positions[7].photos[3].h = 0.6;
  positions[7].photos[3].txt = "BR";
}


ENTRYPOINT void
draw_album (ModeInfo *mi)
{
  album_state *ss = &sss[MI_SCREEN(mi)];
  int dspImg[10];
  int dspImgIndex = 0;
  int i;

  if (!ss->glx_context)
  {
    return;
  }

  glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(ss->glx_context));

  alloc_images(mi);

  ss->now = double_time();

  if (ss->now - ss->img_last_change_time >= image_seconds) 
  {
    int nb_photos = positions[ss->nextPos].count;

    /* Choose the new images to be displayed */
    for (i = 0; i < BUF_IMAGE_SIZE && dspImgIndex < nb_photos; i++) 
    {
      if (ss->images[i] != 0 && ss->images[i]->loaded_p == True && ss->images[i]->used_p == False ) {
        dspImg[dspImgIndex++] = i;
      }
    }
  
    if (dspImgIndex == nb_photos) 
    {
      destroy_images(mi);
      for (i = 0 ; i < nb_photos ; i++) {
        ss->displayedImg[i] = ss->images[dspImg[i]];
        ss->images[dspImg[i]]->used_p = True; 
        if (debug_p)
          fprintf (stderr, "%s: display   img %2d: \"%s\"\n",
                   blurb(), ss->images[dspImg[i]]->id, (ss->images[dspImg[i]]->title ? ss->images[dspImg[i]]->title : "(null)"));

      }
      ss->redisplay_needed_p = True;
      ss->img_last_change_time = ss->now;
      ss->curPos = ss->nextPos;
      ss->nextPos = random() % NB_POSITIONS;
      ss->await_for_images_p = False;
    } 

    ss->now = double_time();

    if (ss->await_for_images_p == True) {
      /*
      glClearColor (0.0, 0.0, 0.0, 0.0);
      glClear (GL_COLOR_BUFFER_BIT);

      glColor4f (0.7, 0.5, 0.3, 1);
      print_gl_string (mi->dpy, ss->xfont, ss->font_dlist,
                       mi->xgwa.width, mi->xgwa.height, 10, mi->xgwa.height - 10,
                       "Loading images...", False);
      */
      glFinish();
      glXSwapBuffers (MI_DISPLAY (mi), MI_WINDOW(mi));
      ss->prev_frame_time = ss->now;
      ss->redisplay_needed_p = False;
      return;
    }
  }

  if (ss->redisplay_needed_p == True)
  { 
    glClearColor (0.0, 0.0, 0.0, 0.0);
    glClear (GL_COLOR_BUFFER_BIT);

    /* Draw the images */
    for (i = 0 ; i < positions[ss->curPos].count ; i++) {
      draw_image(mi, ss->displayedImg[i], positions[ss->curPos].photos[i]);
    }

    glFinish();
    glXSwapBuffers (MI_DISPLAY (mi), MI_WINDOW(mi));
    ss->prev_frame_time = ss->now;
    ss->redisplay_needed_p = False;
  } 
}

XSCREENSAVER_MODULE_2 ("album", album, album)

#endif /* USE_GL */
