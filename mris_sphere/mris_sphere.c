
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "macros.h"
#include "error.h"
#include "diag.h"
#include "proto.h"
#include "mrisurf.h"
#include "mri.h"
#include "macros.h"
#include "utils.h"
#include "timer.h"

static char vcid[]="$Id: mris_sphere.c,v 1.15 1999/06/07 06:57:11 fischl Exp $";

int main(int argc, char *argv[]) ;

static int  get_option(int argc, char *argv[]) ;
static void usage_exit(void) ;
static void print_usage(void) ;
static void print_help(void) ;
static void print_version(void) ;
int MRISscaleUp(MRI_SURFACE *mris) ;

char *Progname ;

static INTEGRATION_PARMS  parms ;
#define BASE_DT_SCALE     1.0
static float base_dt_scale = BASE_DT_SCALE ;
static int nbrs = 2 ;
static int inflate = 0 ;
static double disturb = 0 ;
static int   max_passes = 1 ;
static int   randomly_project = 0 ;
static int   talairach = 1 ;
static float scale = 1.0 ;
static int mrisDisturbVertices(MRI_SURFACE *mris, double amount) ;
static int quick = 0 ;
static int load = 0 ;
static float inflate_area  = 0.0f ;
static float inflate_tol  = 1.0f ;
static int   inflate_avgs = 0 ;
static float inflate_nlarea  = 0.0f ;
static int   inflate_iterations = 200 ;

int
main(int argc, char *argv[])
{
  char         **av, *in_surf_fname, *out_fname, 
               fname[100], *cp ;
  int          ac, nargs, msec ;
  MRI_SURFACE  *mris ;
  struct timeb  then ;

  Gdiag = DIAG_SHOW ;

  TimerStart(&then) ;
  Progname = argv[0] ;
  ErrorInit(NULL, NULL, NULL) ;
  DiagInit(NULL, NULL, NULL) ;

  parms.dt = .1 ;
  parms.projection = PROJECT_ELLIPSOID ;
  parms.tol = 1e-1 ;
  parms.n_averages = 1024 ;
  parms.min_averages = 0 ;
  parms.l_angle = 0.0 /* L_ANGLE */ ;
  parms.l_area = 0.0 /* L_AREA */ ;
  parms.l_neg = 0.0 ;
  parms.l_dist = 1.0 ;
  parms.l_spring = 0.0 ;
  parms.l_area = 1.0 ;
  parms.l_boundary = 0.0 ;
  parms.l_curv = 0.0 ;
  parms.niterations = 25 ;
  parms.write_iterations = 1000 ;
  parms.a = parms.b = parms.c = 0.0f ;  /* ellipsoid parameters */
  parms.dt_increase = 1.01 /* DT_INCREASE */;
  parms.dt_decrease = 0.99 /* DT_DECREASE*/ ;
  parms.error_ratio = 1.03 /*ERROR_RATIO */;
  parms.integration_type = INTEGRATE_LINE_MINIMIZE ;
  parms.momentum = 0.9 ;
  parms.desired_rms_height = -1.0 ;
  parms.base_name[0] = 0 ;
  parms.Hdesired = 0.0 ;   /* a flat surface */
  parms.nbhd_size = 7 ;
  parms.max_nbrs = 8 ;

  ac = argc ;
  av = argv ;
  for ( ; argc > 1 && ISOPTION(*argv[1]) ; argc--, argv++)
  {
    nargs = get_option(argc, argv) ;
    argc -= nargs ;
    argv += nargs ;
  }

  parms.scale = scale ;

  if (argc < 3)
    usage_exit() ;

  parms.base_dt = base_dt_scale * parms.dt ;
  in_surf_fname = argv[1] ;
  out_fname = argv[2] ;

  if (parms.base_name[0] == 0)
  {
    FileNameOnly(out_fname, fname) ;
    cp = strchr(fname, '.') ;
    if (cp)
      strcpy(parms.base_name, cp+1) ;
    else
      strcpy(parms.base_name, "sphere") ;
  }

  mris = MRISread(in_surf_fname) ;
  if (!mris)
    ErrorExit(ERROR_NOFILE, "%s: could not read surface file %s",
              Progname, in_surf_fname) ;


  fprintf(stderr, "reading original vertex positions...\n") ;
  if (!FZERO(disturb))
    mrisDisturbVertices(mris, disturb) ;
  MRISreadOriginalProperties(mris, "smoothwm") ;
  
  fprintf(stderr, "unfolding cortex into spherical form...\n");
  if (talairach)
    MRIStalairachTransform(mris, mris) ;

  if (!load && inflate)
  {
    INTEGRATION_PARMS inflation_parms ;

    memset(&inflation_parms, 0, sizeof(INTEGRATION_PARMS)) ;
    strcpy(inflation_parms.base_name, parms.base_name) ;
    inflation_parms.write_iterations = parms.write_iterations ;
    inflation_parms.niterations = inflate_iterations ;
    inflation_parms.l_spring_norm = 1.0 ;
    inflation_parms.l_nlarea = inflate_nlarea ;
    inflation_parms.l_area = inflate_area ;
    inflation_parms.n_averages = inflate_avgs ;
    inflation_parms.l_sphere = .25 ;
    inflation_parms.a = DEFAULT_RADIUS ;
    inflation_parms.tol = inflate_tol ;
    inflation_parms.integration_type = INTEGRATE_MOMENTUM ;
    inflation_parms.momentum = 0.9 ;
    inflation_parms.dt = 0.9 ;
    
    MRISinflateToSphere(mris, &inflation_parms) ;
    parms.start_t = inflation_parms.start_t ;
  }

  MRISprojectOntoSphere(mris, mris, DEFAULT_RADIUS) ;
  fprintf(stderr,"surface projected - minimizing metric distortion...\n");
  MRISsetNeighborhoodSize(mris, nbrs) ;
  if (quick)
  {
    if (!load)
    {
#if 0
      parms.n_averages = 32 ;
      parms.tol = .1 ;
      parms.l_parea = parms.l_dist = 0.0 ; parms.l_nlarea = 1 ; 
#endif
      MRISprintTessellationStats(mris, stderr) ;
      MRISquickSphere(mris, &parms, max_passes) ;  
    }
#if 0
    MRISripDefectiveFaces(mris) ;
    MRISprintTessellationStats(mris, stderr) ;
    parms.nbhd_size = 1 ; parms.max_nbrs = 8 ;
    parms.l_parea = 1 ; parms.l_nlarea = 0 ; parms.l_dist = 1 ;
    /* was parms->l_dist = 0.1 */
    /*    parms.n_averages = 256 ;*/
    MRISquickSphere(mris, &parms, max_passes) ;  
    MRISresetNeighborhoodSize(mris, 1) ;
    parms.n_averages = 32 ; parms.tol = 1 ;
    MRISprintTessellationStats(mris, stderr) ;
    parms.l_parea = 0.01 ; parms.l_nlarea = 1 ; parms.l_dist = 0.01 ;
    MRISquickSphere(mris, &parms, max_passes) ;  
    MRISprintTessellationStats(mris, stderr) ;
#endif
  }
  else
    MRISunfold(mris, &parms, max_passes) ;  
  if (!load)
  {
    fprintf(stderr, "writing spherical brain to %s\n", out_fname) ;
    MRISwrite(mris, out_fname) ;
  }

  msec = TimerStop(&then) ;
  fprintf(stderr, "spherical transformation took %2.2f hours\n",
          (float)msec/(1000.0f*60.0f*60.0f));
  exit(0) ;
  return(0) ;  /* for ansi */
}

/*----------------------------------------------------------------------
            Parameters:

           Description:
----------------------------------------------------------------------*/
static int
get_option(int argc, char *argv[])
{
  int  nargs = 0 ;
  char *option ;
  float f ;
  
  option = argv[1] + 1 ;            /* past '-' */
  if (!stricmp(option, "-help"))
    print_help() ;
  else if (!stricmp(option, "-version"))
    print_version() ;
  else if (!stricmp(option, "dist"))
  {
    sscanf(argv[2], "%f", &parms.l_dist) ;
    nargs = 1 ;
    fprintf(stderr, "l_dist = %2.3f\n", parms.l_dist) ;
  }
  else if (!stricmp(option, "lm"))
  {
    parms.integration_type = INTEGRATE_LINE_MINIMIZE ;
    fprintf(stderr, "integrating with line minimization\n") ;
  }
  else if (!stricmp(option, "talairach"))
  {
    talairach = 1 ;
    fprintf(stderr, "transforming surface into Talairach space.\n") ;
  }
  else if (!stricmp(option, "dt"))
  {
    parms.dt = atof(argv[2]) ;
    parms.base_dt = base_dt_scale*parms.dt ;
    nargs = 1 ;
    fprintf(stderr, "momentum with dt = %2.2f\n", parms.dt) ;
  }
  else if (!stricmp(option, "curv"))
  {
    sscanf(argv[2], "%f", &parms.l_curv) ;
    nargs = 1 ;
    fprintf(stderr, "using l_curv = %2.3f\n", parms.l_curv) ;
  }
  else if (!stricmp(option, "area"))
  {
    sscanf(argv[2], "%f", &parms.l_area) ;
    nargs = 1 ;
    fprintf(stderr, "using l_area = %2.3f\n", parms.l_area) ;
  }
  else if (!stricmp(option, "iarea"))
  {
    inflate_area = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "inflation l_area = %2.3f\n", inflate_area) ;
  }
  else if (!stricmp(option, "itol"))
  {
    inflate_tol = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "inflation tol = %2.3f\n", inflate_tol) ;
  }
  else if (!stricmp(option, "in"))
  {
    inflate_iterations = atoi(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "inflation iterations = %d\n", inflate_iterations) ;
  }
  else if (!stricmp(option, "iavgs"))
  {
    inflate_avgs = atoi(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "inflation averages = %d\n", inflate_avgs) ;
  }
  else if (!stricmp(option, "inlarea"))
  {
    inflate_nlarea = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "inflation l_nlarea = %2.3f\n", inflate_nlarea) ;
  }
  else if (!stricmp(option, "adaptive"))
  {
    parms.integration_type = INTEGRATE_ADAPTIVE ;
    fprintf(stderr, "using adaptive time step integration\n") ;
  }
  else if (!stricmp(option, "nbrs"))
  {
    nbrs = atoi(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "using neighborhood size=%d\n", nbrs) ;
  }
  else if (!stricmp(option, "spring"))
  {
    sscanf(argv[2], "%f", &parms.l_spring) ;
    nargs = 1 ;
    fprintf(stderr, "using l_spring = %2.3f\n", parms.l_spring) ;
  }
  else if (!stricmp(option, "name"))
  {
    strcpy(parms.base_name, argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "using base name = %s\n", parms.base_name) ;
  }

  else if (!stricmp(option, "angle"))
  {
    sscanf(argv[2], "%f", &parms.l_angle) ;
    nargs = 1 ;
    fprintf(stderr, "using l_angle = %2.3f\n", parms.l_angle) ;
  }
  else if (!stricmp(option, "area"))
  {
    sscanf(argv[2], "%f", &parms.l_area) ;
    nargs = 1 ;
    fprintf(stderr, "using l_area = %2.3f\n", parms.l_area) ;
  }
  else if (!stricmp(option, "tol"))
  {
    if (sscanf(argv[2], "%e", &f) < 1)
      ErrorExit(ERROR_BADPARM, "%s: could not scan tol from %s",
                Progname, argv[2]) ;
    parms.tol = (double)f ;
    nargs = 1 ;
    fprintf(stderr, "using tol = %2.2e\n", (float)parms.tol) ;
  }
  else if (!stricmp(option, "error_ratio"))
  {
    parms.error_ratio = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "error_ratio=%2.3f\n", parms.error_ratio) ;
  }
  else if (!stricmp(option, "dt_inc"))
  {
    parms.dt_increase = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "dt_increase=%2.3f\n", parms.dt_increase) ;
  }
  else if (!stricmp(option, "NLAREA"))
  {
    parms.l_nlarea = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "nlarea = %2.3f\n", parms.l_nlarea) ;
  }
  else if (!stricmp(option, "PAREA"))
  {
    parms.l_parea = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "parea = %2.3f\n", parms.l_parea) ;
  }
  else if (!stricmp(option, "NLDIST"))
  {
    parms.l_nldist = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "nldist = %2.3f\n", parms.l_nldist) ;
  }
  else if (!stricmp(option, "vnum") || !stricmp(option, "distances"))
  {
    parms.nbhd_size = atof(argv[2]) ;
    parms.max_nbrs = atof(argv[3]) ;
    nargs = 2 ;
    fprintf(stderr, "nbr size = %d, max neighbors = %d\n",
            parms.nbhd_size, parms.max_nbrs) ;
  }
  else if (!stricmp(option, "dt_dec"))
  {
    parms.dt_decrease = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "dt_decrease=%2.3f\n", parms.dt_decrease) ;
  }
  else switch (toupper(*option))
  {
  case 'P':
    max_passes = atoi(argv[2]) ;
    fprintf(stderr, "limitting unfolding to %d passes\n", max_passes) ;
    nargs = 1 ;
    break ;
  case 'Q':
    quick = 1 ;
    fprintf(stderr, "doing quick spherical unfolding.\n") ;
    nbrs = 1 ;
    parms.l_spring = parms.l_dist = parms.l_parea = parms.l_area = 0.0 ; 
    parms.l_nlarea = 1.0 ;
    parms.tol = 1e-2 ;
    parms.n_averages = 32 ;
    /*    parms.tol = 10.0f / (sqrt(33.0/1024.0)) ;*/
    break ;
  case 'L':
    load = 1 ;
    break ;
  case 'B':
    base_dt_scale = atof(argv[2]) ;
    parms.base_dt = base_dt_scale*parms.dt ;
    nargs = 1;
    break ;
  case 'S':
    scale = atof(argv[2]) ;
    fprintf(stderr, "scaling brain by = %2.3f\n", (float)scale) ;
    nargs = 1 ;
    break ;
  case 'V':
    Gdiag_no = atoi(argv[2]) ;
    nargs = 1 ;
    break ;
  case 'D':
    disturb = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "disturbing vertex positions by %2.3f\n",(float)disturb) ;
    break ;
  case 'R':
    randomly_project = !randomly_project ;
    fprintf(stderr, "using random placement for projection.\n") ;
    break ;
  case 'M':
    parms.integration_type = INTEGRATE_MOMENTUM ;
    parms.momentum = atof(argv[2]) ;
    nargs = 1 ;
    fprintf(stderr, "momentum = %2.2f\n", (float)parms.momentum) ;
    break ;
  case 'W':
    Gdiag |= DIAG_WRITE ;
    sscanf(argv[2], "%d", &parms.write_iterations) ;
    nargs = 1 ;
    fprintf(stderr, "using write iterations = %d\n", parms.write_iterations) ;
    break ;
  case 'I':
    inflate = 1 ;
    fprintf(stderr, "inflating brain...\n") ;
    break ;
  case 'A':
    sscanf(argv[2], "%d", &parms.n_averages) ;
    nargs = 1 ;
    fprintf(stderr, "using n_averages = %d\n", parms.n_averages) ;
    break ;
  case '?':
  case 'U':
    print_usage() ;
    exit(1) ;
    break ;
  case 'N':
    sscanf(argv[2], "%d", &parms.niterations) ;
    nargs = 1 ;
    fprintf(stderr, "using niterations = %d\n", parms.niterations) ;
    break ;
  default:
    fprintf(stderr, "unknown option %s\n", argv[1]) ;
    exit(1) ;
    break ;
  }

  return(nargs) ;
}

static void
usage_exit(void)
{
  print_usage() ;
  exit(1) ;
}

static void
print_usage(void)
{
  fprintf(stderr, 
          "usage: %s [options] <surface file> <patch file name> <output patch>"
          "\n", Progname) ;
}

static void
print_help(void)
{
  print_usage() ;
  fprintf(stderr, 
       "\nThis program will add a template into an average surface.\n");
  fprintf(stderr, "\nvalid options are:\n\n") ;
  exit(1) ;
}

static void
print_version(void)
{
  fprintf(stderr, "%s\n", vcid) ;
  exit(1) ;
}

static int
mrisDisturbVertices(MRI_SURFACE *mris, double amount)
{
  int    vno ;
  VERTEX *v ;

  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    v->x += randomNumber(-amount, amount) ;
    v->y += randomNumber(-amount, amount) ;
  }

  MRIScomputeMetricProperties(mris) ;
  return(NO_ERROR) ;
}
int
MRISscaleUp(MRI_SURFACE *mris)
{
  int     vno, n, max_v, max_n ;
  VERTEX  *v ;
  float   ratio, max_ratio ;

  max_ratio = 0.0f ; max_v = max_n = 0 ;
  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    if (vno == Gdiag_no)
      DiagBreak() ;
    for (n = 0 ; n < v->vnum ; n++)
    {
      if (FZERO(v->dist[n]))   /* would require infinite scaling */
        continue ;
      ratio = v->dist_orig[n] / v->dist[n] ;
      if (ratio > max_ratio)
      {
        max_v = vno ; max_n = n ;
        max_ratio = ratio ;
      }
    }
  }

  fprintf(stderr, "max @ (%d, %d), scaling brain by %2.3f\n", 
          max_v, max_n, max_ratio) ;
#if 0
  MRISscaleBrain(mris, mris, max_ratio) ;
#else
  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    if (vno == Gdiag_no)
      DiagBreak() ;
    for (n = 0 ; n < v->vnum ; n++)
      v->dist_orig[n] /= max_ratio ;
  }
#endif
  return(NO_ERROR) ;
}

