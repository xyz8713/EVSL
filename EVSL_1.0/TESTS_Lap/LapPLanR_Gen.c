#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "evsl.h"
#include "io.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
int findarg(const char *argname, ARG_TYPE type, void *val, int argc, char **argv);
int lapgen(int nx, int ny, int nz, cooMat *Acoo);
int exeiglap3(int nx, int ny, int nz, double a, double b, int *m, double **vo);
void daxpy_(int *n,double *alpha,double *x,int *incx,double *y,int *incy);
double dnrm2_(int *n,double *x,int *incx);

int main(int argc, char *argv[]) {
  /*------------------------------------------------------------
    generates a laplacean matrix A on an nx x ny x nz mesh and
    matrix B on an (nx x ny) x nz x 1 mesh,
    and computes all eigenvalues of A x = lambda B x
    in a given interval [a  b]
    The default set values are
    nx = 16; ny = 16; nz = 16;
    a = 0.4; b = 0.8;
    nslices = 1 [one slice only] 
    other parameters 
    tol [tolerance for stopping - based on residual]
    Mdeg = pol. degree used for DOS
    nvec  = number of sample vectors used for DOS
    This uses:
    Thick-restart Lanczos with polynomial filtering
    ------------------------------------------------------------*/
  int n, nx, ny, nz, i, j, npts, nslices, nvec, Mdeg, nev, 
      mlan, max_its, ev_int, sl, flg, ierr;
  /* find the eigenvalues of A in the interval [a,b] */
  double a, b, lmax, lmin, ecount, tol, *sli, *mu;
  double xintv[4];
  double *vinit;
  polparams pol;
  FILE *fstats = NULL;
  if (!(fstats = fopen("OUT/LapPLanR_Gen.out","w"))) {
    printf(" failed in opening output file in OUT/\n");
    fstats = stdout;
  }
  /*-------------------- matrices A, B: coo format and csr format */
  cooMat Acoo, Bcoo;
  csrMat Acsr, Bcsr;
  /*-------------------- default values */
  nx   = 8;
  ny   = 8;
  nz   = 8;
  a    = 0.6;
  b    = 0.9;
  nslices = 4;
  //-----------------------------------------------------------------------
  //-------------------- reset some default values from command line [Yuanzhe/]
  /* user input from command line */
  flg = findarg("help", NA, NULL, argc, argv);
  if (flg) {
    printf("Usage: ./testL.ex -nx [int] -ny [int] -nz [int] -a [double] -b [double] -nslices [int]\n");
    return 0;
  }
  findarg("nx", INT, &nx, argc, argv);
  findarg("ny", INT, &ny, argc, argv);
  findarg("nz", INT, &nz, argc, argv);
  findarg("a", DOUBLE, &a, argc, argv);
  findarg("b", DOUBLE, &b, argc, argv);
  findarg("nslices", INT, &nslices, argc, argv);
  fprintf(fstats,"used nx = %3d ny = %3d nz = %3d",nx,ny,nz);
  fprintf(fstats," [a = %4.2f  b= %4.2f],  nslices=%2d \n",a,b,nslices);
  /*-------------------- matrix size */
  n = nx * ny * nz;
  /*-------------------- stopping tol */
  tol  = 1e-8;
  /*-------------------- output the problem settings */
  fprintf(fstats, "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\n");
  fprintf(fstats, "Laplacian A : %d x %d x %d, n = %d\n", nx, ny, nz, n);
  fprintf(fstats, "Laplacian B : %d x %d, n = %d\n", nx*ny,  nz, n);
  fprintf(fstats, "Interval: [%20.15f, %20.15f]  -- %d slices \n", a, b, nslices);
  /*-------------------- generate 2D/3D Laplacian matrix 
   *                     saved in coo format */
  ierr = lapgen(nx, ny, nz, &Acoo);
  ierr = lapgen(nx*ny, nz, 1, &Bcoo);
  /*-------------------- convert coo to csr */
  ierr = cooMat_to_csrMat(0, &Acoo, &Acsr);
  ierr = cooMat_to_csrMat(0, &Bcoo, &Bcsr);
  /*-------------------- start EVSL */
  EVSLStart();
  /*-------------------- set the right-hand side matrix B */
  SetRhsMatrix(&Bcsr);
  /*-------------------- step 0: get eigenvalue bounds */
  //-------------------- initial vector  
  vinit = (double *) malloc(n*sizeof(double));
  rand_double(n, vinit);
  ierr = LanBounds(&Acsr, 60, vinit, &lmin, &lmax);
  fprintf(fstats, "Step 0: Eigenvalue bound s for B^{-1}*A: [%.15e, %.15e]\n", 
          lmin, lmax);
  /*-------------------- interval and eig bounds */
  xintv[0] = a;
  xintv[1] = b;
  xintv[2] = lmin;
  xintv[3] = lmax;
  /*-------------------- call kpmdos to get the DOS for dividing the spectrum*/
  /*-------------------- define kpmdos parameters */
  Mdeg = 40;
  nvec = 100;
  mu = malloc((Mdeg+1)*sizeof(double));
  //-------------------- call kpmdos 
  double t = cheblan_timer();
  ierr = kpmdos(&Acsr, Mdeg, 1, nvec, xintv, mu, &ecount);
  t = cheblan_timer() - t;
  if (ierr) {
    printf("kpmdos error %d\n", ierr);
    return 1;
  }
  fprintf(fstats, " Time to build DOS (kpmdos) was : %10.2f  \n",t);
  fprintf(fstats, " estimated eig count in interval: %.15e \n",ecount);
  //-------------------- call splicer to slice the spectrum
  npts = 10 * ecount; 
  sli = malloc((nslices+1)*sizeof(double));

  fprintf(fstats,"DOS parameters: Mdeg = %d, nvec = %d, npnts = %d\n",Mdeg, nvec, npts);
  ierr = spslicer(sli, mu, Mdeg, xintv, nslices,  npts);
  if (ierr) {
    printf("spslicer error %d\n", ierr);
    return 1;
  }
  printf("====================  SLICES FOUND  ====================\n");
  for (j=0; j<nslices;j++) {
    printf(" %2d: [% .15e , % .15e]\n", j+1, sli[j],sli[j+1]);
  }
  //-------------------- # eigs per slice
  ev_int = (int) (1 + ecount / ((double) nslices));
  //-------------------- For each slice call ChebLanr
  for (sl =0; sl<nslices; sl++){
    printf("======================================================\n");
    int nev2;
    double *lam, *Y, *res;
    int *ind;
    //-------------------- 
    a = sli[sl];
    b = sli[sl+1];
    printf(" subinterval: [%.4e , %.4e]\n", a, b); 
    //-------------------- approximate number of eigenvalues wanted
    nev = ev_int+2;
    //-------------------- Dimension of Krylov subspace 
    mlan = max(4*nev, 100);
    mlan = min(mlan, n);
    max_its = 3*mlan;
    //-------------------- ChebLanTr
    xintv[0] = a;     xintv[1] = b;
    xintv[2] = lmin;  xintv[3] = lmax;
    //-------------------- set up default parameters for pol.      
    set_pol_def(&pol);
    //-------------------- this is to show how you can reset some of the
    //                     parameters to determine the filter polynomial
    pol.damping = 0;
    //-------------------- use a stricter requirement for polynomial
    pol.thresh_int = 0.25;
    pol.thresh_ext = 0.15;
    pol.max_deg  = 300;
    // pol.deg = 20 //<< this will force this exact degree . not recommended
    //                   it is better to change the values of the thresholds
    //                   pol.thresh_ext and plot.thresh_int
    //-------------------- Now determine polymomial to use
    find_pol(xintv, &pol);       

    fprintf(fstats, " polynomial deg %d, bar %e gam %e\n",pol.deg,pol.bar, pol.gam);
    //-------------------- then call ChenLanNr
    ierr = ChebLanTr(&Acsr, mlan, nev, xintv, max_its, tol, vinit,
                     &pol, &nev2, &lam, &Y, &res, fstats);
    if (ierr) {
      printf("ChebLanTr error %d\n", ierr);
      return 1;
    }

    /* compute residual: r = A*x - lam*B*x */
    double *r = (double *) malloc(n*sizeof(double));
    double *w = (double *) malloc(n*sizeof(double));
    int one = 1;
    for (i=0; i<nev2; i++) {
      double *y = Y+i*n;
      double t = -lam[i];
      matvec_csr(&Acsr, y, r);
      matvec_csr(&Bcsr, y, w);
      daxpy_(&n, &t, w, &one, r, &one);
      res[i] = dnrm2_(&n, r, &one);
    }
    free(r);
    free(w);

    /* sort the eigenvals: ascending order
     * ind: keep the orginal indices */
    ind = (int *) malloc(nev2*sizeof(int));
    sort_double(nev2, lam, ind);
    printf(" number of eigenvalues found: %d\n", nev2);

    /* print eigenvalues */
    fprintf(fstats, "     Eigenvalues in [a, b]\n");
    fprintf(fstats, "    Computed [%d]        ||Res||              ", nev2);
    fprintf(fstats, "\n");
    for (i=0; i<nev2; i++) {
      fprintf(fstats, "% .15e  %.1e\n", lam[i], res[ind[i]]);
      if (i>50) {
        fprintf(fstats,"                        -- More not shown --\n");
        break;
      } 
    }
    //-------------------- free allocated space withing this scope
    if (lam)  free(lam);
    if (Y)  free(Y);
    if (res)  free(res);
    free_pol(&pol);
    free(ind);
  }
  //-------------------- free other allocated space 
  free(vinit);
  free(sli);
  free_coo(&Acoo);
  free_csr(&Acsr);
  free_coo(&Bcoo);
  free_csr(&Bcsr);
  free(mu);
  fclose(fstats);

  /*-------------------- finalize EVSL */
  EVSLFinish();

  return 0;
}

