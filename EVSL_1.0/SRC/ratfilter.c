#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include "def.h"
#include "blaslapack.h"
#include "struct.h"
#include "internal_proto.h"

/**------------------------- Cauchy integration-based filter --------------
 * @brief Compute the locations of the poles
 *
 * @param method    0 for Guass Legendre; 1 for midpoint
 * @param n         Number of poles in the upper half plane
 * @param[out] zk   Vector of pole locations
 *
 *----------------------------------------------------------------------*/
void contQuad(int method, int n, complex double* zk) {
  int i, m, INFO;
  double *beta, *D, *Z, *WORK;
  char JOBZ = 'V';
  complex double tmp2;
  if (method == 0) {
    m = n-1;
    Malloc(D, n, double);
    Malloc(Z, n*n, double);
    Malloc(WORK, 2*n-2, double);
    for (i=0; i<n; i++) {
      D[i] = 0.0;
    }
    Malloc(beta, m, double);
    for (i=0; i<m; i++) {
      beta[i] = 0.5/(sqrt(1-pow(2*(i+1),-2)));
    }
    dstev_(&JOBZ, &n, D, beta, Z, &n, WORK, &INFO);
    for (i=0; i<n; i++) {
      tmp2 = I*M_PI/2.0*(1.0-D[i]);
      zk[i] = cexp(tmp2);
    }
    free(beta);
    free(D);
    free(Z);
    free(WORK);
  } else if (method == 1) {
    for (i=0; i<n; i++) {
      tmp2 = M_PI*I*(2*(i+1)-1)/(2.0*n);
      zk[i] = cexp(tmp2);
    }
  }
}

  /**------------------Multiple pole rational filter evaluation --------------
   * @brief Compute the function value of the multiple pole rational filter at real locations 
   * @param n      number of the pole
   * @param mulp   multiplicity of the pole
   * @param zk     array containing the poles.
   * @param alp    fractional expansion coefficients
   * @param m      number of locations to be evaluated
   * @param z      real locations to be evaluated
   *
   * @param[out] xx    : function values at real locations z
   *
   *----------------------------------------------------------------------*/
void ratf2p2(int n, int *mulp, complex double *zk, complex double* alp, int m,
             double *z, double *xx) {
  complex double y, x, t;
  int i, j, k, k1, k2;
  for (k2=0; k2<m; k2++) {
    k = 0;
    y = 0.0 + 0.0*I;
    for (j=0; j<n; j++) {
      x = 1.0 / (z[k2]-zk[j]);
      k1 = k + mulp[j];
      t = 0.0+0.0*I;
      for (i=k1-1;i>=k;i--) {
        t = x*(alp[i]+t);
      }
      y = y+t;
      k = k1;
    }
    xx[k2] = 2.0*creal(y);
  }
}



/**
 * @brief Get the fraction expansion of 1/[(z-s1)^k1 (z-s2)^k2]
 * */
void pfe2(complex double s1, complex double s2, int k1, int k2, 
          complex double* alp, complex double* bet) {
  int i;
  complex double d, xp;
  if (cabs(s1-s2) < 1.0e-12 * (cabs(s1)+cabs(s2))) {
    for (i=0; i<k1+k2; i++) {
      alp[i] = 0.0;
    }
    alp[k1+k2-1] = 1;
  } else if ((k1 == 1) && (k2 == 1)) {
    d = s1-s2;
    alp[k1-1] = 1.0 / d;
    bet[k1-1] = -alp[k1-1];
  } else {
    d = 1.0 + 0.0*I;
    xp = 1.0 / cpow((s1-s2),k2);
    for (i=0; i<k1; i++) {
      alp[k1-i-1] = d * xp;
      xp = xp / (s1-s2);
      d = -d * (k2+i) / (i+1.0);
    }
    d = 1.0 + 0.0*I;
    xp = 1.0 / cpow((s2-s1),k1);
    for (i=0; i<k2; i++) {
      bet[k2-i-1] = d * xp;
      xp = xp / (s2-s1);
      d = -d * (k1+i) / (i+1.0);
    }
  }
}

  /**
   * @brief Integration of 1/[(z-s1)^k1 (z-s2)^k2] from a to b
   */
complex double integg2(complex double s1, complex double s2, 
                       complex double* alp, int k1, complex double* bet, 
                       int k2, double a, double b) {
  complex double t, t1, t0, scal;
  int k;
  t = 0.0 + 0.0*I;
  t1 = 0.0 + 0.0*I;
  t0 = 0.0 +0.0*I;
  for (k=0; k<k1; k++) {
    scal = alp[k];
    if (k==0) {
      t0 = scal*clog((b-s1)/(a-s1));
    } else {
      t = t - (scal*1.0/k) * (1.0/cpow((b-s1),k)-1.0/cpow((a-s1),k));    
    }
  }
  for (k=0; k<k2; k++) {
    scal = bet[k];
    if (k==0) {
      t1 = scal*clog((b-s2)/(a-s2));
    } else {
      t = t - (scal*1.0/k)*(1.0/cpow((b-s2),k)-1.0/cpow((a-s2),k));    
    }
  }
  t = t + (t0+t1);
  return t;
}


/**
 *------------------multiple pole LS rational filter weights--------------
 * @brief Compute the LS weight for each multiple pole
 * @param n      number of poles in the upper half plane
 * @param zk     pole locations
 * @param mulp    multiplicity of each pole
 * @param lambda LS integration weight for [-1, 1]
 *
 * @param[out] omega LS weight for each pole
 *
 *----------------------------------------------------------------------*/
void weights(int n, complex double* zk, int* mulp, double lambda, 
             complex double* omega) {
  int INFO;
  int nrhs = 1;
  int *ipiv;
  int m;
  double mu = 10.0;
  int i, j, ii, jj, ki, kj, n1, n2, nf=0, k1, k2;
  complex double s1, s2, s3, t;
  complex double *rhs, *A, *B, *mat, *alp, *bet;
  double scaling;
  for (i=0; i<n; i++) {
    nf += mulp[i];
  }
  m = 2*nf;
  Malloc(ipiv, m, int);
  Malloc(rhs, m, complex double);
  Malloc(A, nf*nf, complex double);
  Malloc(B, nf*nf, complex double);
  Malloc(mat, 4*nf*nf, complex double);
  for(i=0; i<nf; i++) {
    for(j=0; j<nf; j++) {
      A[i*nf+j] = 0.0 + 0.0*I;
      B[i*nf+j] = 0.0 + 0.0*I;
    }
  }
  if (fabs(lambda) < 1.0e-12) {
    lambda = 1.0e-5;
  }
  ki = 0;
  for (ii=0; ii<n; ii++) {
    s1 = zk[ii];
    n1 = mulp[ii];
    kj = 0;
    s3 = conj(s1);
    for (i=0; i<n1; i++) {
      if (i==0) {
	rhs[ki+i] = lambda*clog((s3-1.0)/(s3+1.0));
      } else {
	rhs[ki+i] = -lambda*(1.0/(i))*(1/cpow((1.0-s3),i)-1.0/cpow((-1.0-s3),i));
      }
    }
    for (jj=0; jj<n; jj++) {
      s2 = zk[jj];
      n2 = mulp[jj];
      for (i=0; i<n1; i++) {
	for (j=0; j<n2; j++) {
	  s3 = conj(s2);
	  if (cabs(s1-s3) < 1.0e-12*(cabs(s1)+cabs(s3))) {
            Malloc(alp, i+j+2, complex double);
            Malloc(bet, 1, complex double);
	    k1 = i+1+j+1;
	    k2 = 0;
	  } else {
            Malloc(alp, i+1, complex double);
            Malloc(bet, j+1, complex double);
	    k1 = i+1;
	    k2 = j+1;
	  }
	  pfe2(s1, s3, k1, k2, alp, bet);
	  t = integg2(s1, s3, alp, k1, bet, k2, -mu, mu);
	  t += (lambda-1)*integg2(s1, s3, alp, k1, bet, k2, -1.0, 1.0);
	  A[(ki+i)*nf+kj+j] = t;
	  free(bet);
	  free(alp);
	  if (cabs(s1-s2) < 1.0e-12*(cabs(s1)+cabs(s2))) {
            Malloc(alp, i+j+2, complex double);
            Malloc(bet, 1, complex double);
	    k1 = i+1+j+1;
	    k2 = 0;
	  } else {
            Malloc(alp, i+1, complex double);
            Malloc(bet, j+1, complex double);
	    k1 = i+1;
	    k2 = j+1;
	  }
	  pfe2(s1, s2, k1, k2, alp, bet);
	  t = integg2(s1, s2, alp, k1, bet, k2, -mu, mu);
	  t += (lambda-1)*integg2(s1, s2, alp, k1, bet, k2, -1.0, 1.0);
	  B[(ki+i)*nf+kj+j] = t;
	  free(alp);
	  free(bet);
	}
      }
      kj = kj+n2;
    }
    ki = ki+n1;
  }
  for (i=nf; i<2*nf; i++) {
    rhs[i] = conj(rhs[i-nf]);
  }
  /*---form mat = [A,B;conj(B),conj(A)]---*/
  /* Note that mat is stored column-wise for lapack routine */
  for (i=0; i<nf; i++) {
    for(j=0; j<nf; j++) {
      mat[i+j*m] = conj(A[i*nf+j]);
    }
  }
  for (i=0; i<nf; i++) {
    for (j=nf; j<m; j++) {
      mat[i+j*m] = conj(B[i*nf+j-nf]);
    }
  }
  for (i=nf; i<m; i++) {
    for (j=0; j<nf; j++) {
      mat[i+j*m] = B[(i-nf)*nf+j];
    }
  }
  for (i=nf; i<m; i++) {
    for (j=nf; j<m; j++) {
      mat[i+j*m] = A[(i-nf)*nf+j-nf];
    }
  } 
  zgesv_(&m, &nrhs, mat, &m, ipiv, rhs, &m, &INFO);
  for(i=0;i<nf;i++) {
    omega[i] = rhs[i];
  }

  /* Scale coefs to let the filter pass through [-1, 0.5] */
  double aa = 1.0;
  ratf2p2(n, mulp, zk, omega, 1, &aa, &scaling);
  scaling = 0.5 / scaling;
  for (i=0; i<nf; i++) {
    omega[i] *= scaling;
  }

  free(A);
  free(B);
  free(rhs);
  free(mat);  
  free(ipiv);
}


  /**------------------Transform poles and weights computed on [-1, 1] to [a, b] ----------
   * @brief  Compute the weights and pole locations on [a, b]
   * @param n     number of poles used in the upper half plane
   * @param a,b   [a, b] is the interval of desired eigenvalues
   * @param zk    location of the poles
   * @param mulp   multiplicity of the poles
   *
   * @param[out] omegaM: multiple LS weights
   *
   *----------------------------------------------------------------------*/
int scaleweigthts(int n, double a, double b, complex double *zk, int* mulp, 
                  complex double* omegaM) {
  int i, j, k, nf=0;
  double c, h;
  c = 0.5 * (a + b);
  h = 0.5 * (b - a);
  for (i=0; i<n; i++) {
    nf += mulp[i];
    zk[i] = h*zk[i]+c;
  }
  /* Transform the coefs for multiple poles */
  double tmp;
  k = -1;
  for (i=0; i<n; i++) {
    for (j=0; j<mulp[i]; j++) {
      k = k+1;
      omegaM[k] = omegaM[k]*cpow(h,j+1);
    }
  }
  /* Scale ration function to let it pass through [a, 1/2] */
  ratf2p2(n, mulp, zk, omegaM, 1, &a, &tmp);
  tmp = 0.5 / tmp;
  for (i=0; i<nf; i++) {
    omegaM[i] = omegaM[i] * tmp;
  }
  return 0;
}


/**
 * @brief Sets default values for ratparams struct
 * */
void set_ratf_def(ratparams *rat) {
  // -------------------- this sets default values for ratparams struct.
  rat->num = 1;            // number of the poles
  rat->pw = 2;             // default multplicity of each pole
  rat->method = 1;         // using poles from mid-point rule
  rat->beta = 0.01;        // beta in LS approximation
  rat->bar  = 0.5;         // this is fixed for rational filter  
  rat->aa =  -1.0;         // left endpoint of interval
  rat->bb = 1.0;           // right endpoint of interval 
  //rat->cc = 0.0;           // center of interval
  //rat->dd = 1.0;           // width of interval
}

/**----------------------------------------------------------------------
 * @param intv  = an array of length 4 
 *         [intv[0], intv[1]] is the interval of desired eigenvalues
 *         [intv[2], intv[3]] is the global interval of all eigenvalues
 *         it must contain all eigenvalues of A
 *  
 * OUT:
 * @param[out] rat
 * these are set in rat struct:\n
 *   omega : expansion coefficients of rational filter \n
 *    zk   : location of the poles used\n
 *    aa  : adjusted left endpoint\n
 *    bb  : adjusted right endpoint\n
 *    dd  : half-width and.. \n
 *    cc  : ..center of interval\n
 *
 *--------------------------------------------------------------------*/
int find_ratf(double *intv, ratparams *rat) {
  complex double *omega; // weights of the poles
  complex double *zk;    // location of the poles
  int *mulp;             // multiplicity of the each pole
  int n = rat->num, i, pow = 0, pw = rat->pw, method = rat->method;
  double beta = rat->beta;
  /*-------------------- A few parameters to be set or reset */
  Malloc(mulp, n, int);
  Malloc(zk, n, complex double);
  for (i=0; i<n; i++) { // set the multiplicity of each pole
    mulp[i] = pw;
    pow += mulp[i];
  }
  rat->zk = zk;
  rat->mulp = mulp;
  rat->pow = pow; // total multiplicity of the poles
  Malloc(omega, pow, complex double);
  rat->omega = omega;
  //-------------------- intervals related
  if (check_intv(intv, stdout) < 0) {
    return -1;
  }
  double aa, bb;
  aa = max(intv[0], intv[2]);  bb = min(intv[1], intv[3]);
  if (intv[0] < intv[2] || intv[1] > intv[3]) {
    fprintf(stdout, " warning [%s (%d)]: interval (%e, %e) is adjusted to (%e, %e)\n", 
	    __FILE__, __LINE__, intv[0], intv[1], aa, bb);
  }
  //double lmin = intv[2], lmax = intv[3];
  /*-------------------- */
  rat->aa = aa;
  rat->bb = bb; 
  /*-------------------- cc, rr: center and half-width of [aa, bb] */
  //double cc = 0.5 * (aa + bb);
  //double dd = 0.5 * (bb - aa);
  //rat->cc = cc;
  //rat->dd = dd;
  /*------------ compute the location of the poles */
  contQuad(method, n, zk);
  /*------------ compute expansion coefficients of rational filter on [-1, 1] */
  weights(n, zk, mulp, beta, omega);
  /*-------------------- compute expansion coefficients on [aa, bb]*/
  scaleweigthts(n, aa, bb, zk, mulp, omega);
    
  rat->solshift = NULL;
  rat->solshiftdata = NULL;

  return 0;
}

int set_ratf_solfunc(ratparams *rat, csrMat *A, linSolFunc *funcs, void **data) {
  int i,err;
  /* (re)allocate enough space (number of poles) */
  Realloc(rat->solshift, rat->num, linSolFunc);
  Realloc(rat->solshiftdata, rat->num, void *);
  /* if funcs are not provided, use the default sovler: UMFPACK */
  if (funcs == NULL) {
#ifdef EVSL_WITH_SUITESPARSE
    rat->use_default_solver = 1;
    err = set_ratf_solfunc_default(A, rat);
#else
    printf("error: EVSL was not compiled with the default solver, ");
    printf("so the users must provide solvers\n");
    err = -1;
#endif
    return err;
  }
  /* if funcs are provided, copy the function pointers and data */
  rat->use_default_solver = 0;
  for (i=0; i<rat->num; i++) {
    rat->solshift[i] = funcs[i];
    rat->solshiftdata[i] = data ? data[i] : NULL;
  }
  return 0;
}

void free_rat(ratparams *rat) {
  free(rat->mulp);
  free(rat->omega);
  free(rat->zk);
  free(rat->solshift);
#ifdef EVSL_WITH_SUITESPARSE
  free_rat_default_sol(rat);
#endif
  free(rat->solshiftdata);
}

