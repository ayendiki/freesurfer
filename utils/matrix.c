 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <math.h>

#include "nr.h"
#include "matrix.h"
#include "proto.h"
#include "error.h"
#include "utils.h"
#include "diag.h"
#include "eigen.h"
#include "macros.h"

MATRIX *
MatrixCopy(MATRIX *mIn, MATRIX *mOut)
{
  int row, rows, cols ;

  if(mIn == NULL)
    return(NULL);

  rows = mIn->rows ;
  cols = mIn->cols ;

  if (!mOut)
    mOut = MatrixAlloc(rows, cols, mIn->type) ;

  for (row = 1 ; row <= rows ; row++)
    memcpy((char *)(mOut->rptr[row]), (char *)mIn->rptr[row], 
           (cols+1)*sizeof(float)) ;

  return(mOut) ;
}

#define MAX_ELTS 10*1024
  
MATRIX * 
MatrixInverse(MATRIX *mIn, MATRIX *mOut)
{
  float  **a, **y, d, col[MAX_ELTS] ;
  int    i, j, index[MAX_ELTS], rows, cols, alloced = 0 ;
  MATRIX *mTmp ;

  if (mIn->rows != mIn->cols)
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixInverse: matrix (%d x %d) is not square\n",
                 mIn->rows, mIn->cols)) ;

  rows = mIn->rows ;
  cols = mIn->cols ;

  if (!mOut)
  {
    alloced = 1 ;
    mOut = MatrixAlloc(rows, cols, mIn->type) ;
  }
    
  /* allocate temp matrix so as not to destory contents of mIn */
  if (mIn->type == MATRIX_COMPLEX)
  {
    MATRIX *mQuad, *mInv, *mReal, *mImag ;

    mTmp = MatrixAlloc(2*rows, 2*cols, MATRIX_REAL) ;


/*
   form square matrix of the form

   A  -C
   C  A

   where A and C are the real and imaginary components of the input
   matrix respectively.
*/
    MatrixCopyRealRegion(mIn, mTmp, 1, 1, rows, cols, 1, 1) ;
    MatrixCopyRealRegion(mIn, mTmp, 1, 1, rows, cols, rows+1, cols+1) ;
    mQuad = MatrixAlloc(rows,cols,MATRIX_REAL) ;
    MatrixCopyImagRegion(mIn, mQuad, 1, 1, rows, cols, 1, 1) ;
    MatrixScalarMul(mQuad, -1.0f, mQuad) ;
    MatrixCopyRegion(mQuad, mTmp, 1, 1, rows, cols, 1, cols+1) ;
    MatrixCopyImagRegion(mIn, mTmp, 1, 1, rows, cols, cols+1, 1) ;

#if 0
    DiagPrintf(DIAG_MATRIX, "\nextraction\n") ;
    MatrixPrint(stdout, mTmp) ;
#endif

    mReal = MatrixAlloc(rows, cols, MATRIX_REAL) ;
    mImag = MatrixAlloc(rows, cols, MATRIX_REAL) ;
    mInv = MatrixInverse(mTmp, NULL) ;

    MatrixCopyRegion(mInv, mReal, 1, 1, rows, cols, 1, 1) ;
    MatrixCopyRegion(mInv, mImag, rows+1, 1, rows, cols, 1, 1) ;
    MatrixRealToComplex(mReal, mImag, mOut) ;

#if 0
    DiagPrintf(DIAG_MATRIX, "\ninverse of extraction\n") ;
    MatrixPrint(stderr, mInv) ;
    DiagPrintf(DIAG_MATRIX, "\ninverse:\n") ;
    MatrixPrint(stderr, mOut) ;
    DiagPrintf(DIAG_MATRIX, "real + imag = \n") ;
    MatrixPrint(stderr, mReal) ;
    MatrixPrint(stderr, mImag) ;
#endif
    MatrixFree(&mQuad) ;
    MatrixFree(&mReal) ;
    MatrixFree(&mImag) ;
  }
  else
  {
    mTmp = MatrixCopy(mIn, NULL) ;   

    a = mTmp->rptr ;
    y = mOut->rptr ;

#if 0    
    index = (int *)calloc(rows+1, sizeof(int)) ;
    col = (float *)calloc(rows+1, sizeof(float)) ;
#endif
    if (ludcmp(a, rows, index, &d) < 0)
    {
#if 0
      free(index) ; free(col) ;
#endif
      MatrixFree(&mTmp) ;
      if (alloced)
        MatrixFree(&mOut) ;
      return(NULL) ;
    }
    
    for (j = 1 ; j <= rows ; j++)
    {
      for (i = 1 ; i <= rows ; i++)
        col[i] = 0.0f ;
      col[j] = 1.0f ;
      lubksb(a, rows, index, col) ;
      for (i = 1 ; i <= rows ; i++)
        y[i][j] = col[i] ;
    }
#if 0
    free(col) ; free(index) ;
#endif
  }

  MatrixFree(&mTmp) ;

  for (j = 1 ; j <= rows ; j++)
  {
    for (i = 1 ; i <= rows ; i++)
      switch (mOut->type)
      {
      case MATRIX_REAL:
        if (!finite(*MATRIX_RELT(mOut, i, j)))
        {
          if (alloced)
            MatrixFree(&mOut) ;
          return(NULL) ;   /* was singular */
        }
        break ;
      case MATRIX_COMPLEX:
        if (!finite(MATRIX_CELT_REAL(mOut, i, j)) ||
             !finite(MATRIX_CELT_IMAG(mOut, i, j)))
        {
          if (alloced)
            MatrixFree(&mOut) ;
          return(NULL) ;   /* was singular */
        }
        break ;
      }
  }

  return(mOut) ;
}

MATRIX *
MatrixAlloc(int rows, int cols, int type)
{
  MATRIX *mat ;
  int    row, nelts ;

  mat = (MATRIX *)calloc(1, sizeof(MATRIX)) ;
  if (!mat)
    ErrorExit(ERROR_NO_MEMORY, 
              "MatrixAlloc(%d, %d, %d): could not allocate mat", 
              rows, cols, type) ;

  mat->rows = rows ;
  mat->cols = cols ;
  mat->type = type ;

/*
  allocate a single array the size of the matrix, then initialize
  row pointers to the proper locations.
*/

  nelts = rows*cols ;
  if (type == MATRIX_COMPLEX)
    nelts *= 2 ;

/*
  because NRC is one-based, we must leave room for a few unused
  (but valid) addresses before the start of the actual data so
  that mat->rptr[0][0] is a valid address even though it wont
  be used.
*/
  mat->data = (float *)calloc(nelts+2, sizeof(float)) ;
  if (!mat->data)
  {
    fprintf(stderr, "MatrixAlloc(%d, %d): allocation failed\n",
      rows, cols) ;
    exit(1) ;
  }
  mat->data += 2 ;  

/* 
  silly numerical recipes in C requires 1-based stuff. The full
  data array is zero based, point the first row to the zeroth
  element, and so on.
*/
  mat->rptr = (float **)calloc(rows+1, sizeof(float *)) ;
  if (!mat->rptr)
  {
    free(mat->data) ;
    free(mat) ;
    ErrorExit(ERROR_NO_MEMORY, "MatrixAlloc(%d, %d): could not allocate rptr",
              rows, cols) ;
  }
  for (row = 1 ; row <= rows ; row++)
  {
    switch (type)
    {
      case MATRIX_REAL:
        mat->rptr[row] = mat->data + (row-1)*cols - 1 ;
        break ;
      case MATRIX_COMPLEX:
        mat->rptr[row] = (float *)(((CPTR)mat->data) + 
            (row-1)*cols - 1) ;
        break ; 
      default:
        ErrorReturn(NULL,
                    (ERROR_BADPARM, "MatrixAlloc: unknown type %d\n",type)) ;
    }
  }

  return(mat) ;
}

int
MatrixFree(MATRIX **pmat)
{
  MATRIX *mat ;

  mat = *pmat ;
  *pmat = NULL;

  if (!mat)
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "MatrixFree: NULL POINTER!\n"));

  /* silly numerical recipes in C requires 1-based stuff */
  mat->data -= 2 ;
  free(mat->data) ;
  free(mat->rptr) ;
  free(mat) ;

  return(0) ;
}

MATRIX *
MatrixMultiply(MATRIX *m1, MATRIX *m2, MATRIX *m3)
{
  int   col, row, i, rows, cols, m1_cols ;
  float *r3 ;
  register float val, *r1, *r2 ;
  MATRIX   *m_tmp1 = NULL, *m_tmp2 = NULL ;

  if (m1->cols != m2->rows)
    ErrorReturn(NULL,
                (ERROR_BADPARM,
                "MatrixMultiply: m1 cols %d does not match m2 rows %d\n",
                m1->cols, m2->rows)) ;

  if (!m3)
  {
    /* twitzel also did something here */
    if((m1->type == MATRIX_COMPLEX) || (m2->type == MATRIX_COMPLEX)) 
      m3 = MatrixAlloc(m1->rows, m2->cols, MATRIX_COMPLEX);
    else
      m3 = MatrixAlloc(m1->rows, m2->cols, m1->type) ;
    if (!m3)
      return(NULL) ;
  }
  else if ((m3->rows != m1->rows) || (m3->cols != m2->cols))
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixMultiply: (%d x %d) * (%d x %d) != (%d x %d)\n",
                 m1->rows, m1->cols, m2->rows, m2->cols, m3->rows, m3->cols)) ;

  if (m3 == m2)
  {
    m_tmp1 = MatrixCopy(m2, NULL) ;
    m2 = m_tmp1 ;
  }
  if (m3 == m1)
  {
    m_tmp2 = MatrixCopy(m1, NULL) ;
    m1 = m_tmp2 ;
  }
  /*  MatrixClear(m3) ;*/
  cols = m3->cols ;
  rows = m3->rows ;
  m1_cols = m1->cols ;

  /* twitzel modified here */
  if((m1->type == MATRIX_REAL) && (m2->type == MATRIX_REAL))
  {
    for (row = 1 ; row <= rows ; row++)
    {
      r3 = &m3->rptr[row][1] ;
      for (col = 1 ; col <= cols ; col++)
      {
        val = 0.0 ;
        r1 = &m1->rptr[row][1] ;
        r2 = &m2->rptr[1][col] ;
        for (i = 1 ; i <= m1_cols ; i++, r2 += cols)
        {
#if 0
          m3->rptr[row][col] +=
            m1->rptr[row][i] * m2->rptr[i][col] ;
#else
          val += *r1++ * *r2 ;
#endif
        }
        *r3++ = val ;
      }
    }
  } else if((m1->type == MATRIX_COMPLEX) && (m2->type == MATRIX_COMPLEX)) {
 
    for (row = 1 ; row <= rows ; row++)
      {
  for (col = 1 ; col <= cols ; col++)
    {
      for (i = 1 ; i <= m1->cols ; i++)
        {
    float a, b, c, d ;  /* a + ib and c + id */
    
    a = MATRIX_CELT_REAL(m1,row,i) ;
    b = MATRIX_CELT_IMAG(m1,row,i) ;
    c = MATRIX_CELT_REAL(m2,i,col) ;
    d = MATRIX_CELT_IMAG(m2,i,col) ;
    MATRIX_CELT_REAL(m3,row,col) += a*c - b*d ;
    MATRIX_CELT_IMAG(m3,row,col) += a*d + b*c ;
        }
    }
      }
  } else if((m1->type == MATRIX_REAL) && (m2->type == MATRIX_COMPLEX)) {
    for (row = 1 ; row <= rows ; row++)
      {
  for (col = 1 ; col <= cols ; col++)
    {
      for (i = 1 ; i <= m1->cols ; i++)
        {
    float a, c, d ;  /* a + ib and c + id and b=0 here*/
    
    a = *MATRIX_RELT(m1,row,i);
    c = MATRIX_CELT_REAL(m2,i,col);
    d = MATRIX_CELT_IMAG(m2,i,col);
    MATRIX_CELT_REAL(m3,row,col) += a*c;
    MATRIX_CELT_IMAG(m3,row,col) += a*d;
        }
    }
      }
  }
  if (m_tmp1)
    MatrixFree(&m_tmp1) ;
  if (m_tmp2)
    MatrixFree(&m_tmp2) ;
  return(m3) ;
}

int
MatrixPrint(FILE *fp, MATRIX *mat)
{
  int  row, col, rows, cols ;

  rows = mat->rows ;
  cols = mat->cols ;

  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      switch (mat->type)
      {
        case MATRIX_REAL:
          fprintf(fp, "% 2.3f", mat->rptr[row][col]) ;
          break ;
        case MATRIX_COMPLEX:
          fprintf(fp, "% 2.3f + % 2.3f i", 
            MATRIX_CELT_REAL(mat,row,col), 
            MATRIX_CELT_IMAG(mat, row, col)) ;
          break ;
        default:
          ErrorReturn(ERROR_BADPARM,
                      (ERROR_BADPARM, 
                       "MatrixPrint: unknown type %d\n",mat->type)) ;
      }
#if 0
      if (col < cols)
        fprintf(fp, " | ") ;
#else
      if (col < cols)
        fprintf(fp, "  ") ;
#endif
    }
    fprintf(fp, ";\n") ;
  }
  return(NO_ERROR) ;
}
int
MatrixPrintTranspose(FILE *fp, MATRIX *mat)
{
  int  row, col, rows, cols ;

  rows = mat->rows ;
  cols = mat->cols ;

  for (col = 1 ; col <= cols ; col++)
  {
    for (row = 1 ; row <= rows ; row++)
    {
      switch (mat->type)
      {
        case MATRIX_REAL:
          fprintf(fp, "% 2.3f", mat->rptr[row][col]) ;
          break ;
        case MATRIX_COMPLEX:
          fprintf(fp, "% 2.3f + % 2.3f i", 
            MATRIX_CELT_REAL(mat,row,col), 
            MATRIX_CELT_IMAG(mat, row, col)) ;
          break ;
        default:
          ErrorReturn(ERROR_BADPARM,
                      (ERROR_BADPARM, 
                       "MatrixPrint: unknown type %d\n",mat->type)) ;
      }
#if 0
      if (row < rows)
        fprintf(fp, " | ") ;
#else
      if (row < rows)
        fprintf(fp, "  ") ;
#endif
    }
    fprintf(fp, "\n") ;
  }
  return(NO_ERROR) ;
}

MATRIX *
MatrixReadTxt(char *fname, MATRIX *mat)
{
  FILE   *fp ;
  int     rows, cols, row, col ;
  char    *cp, line[400] ;

  fp = fopen(fname, "r") ;
  if (!fp)
    ErrorReturn(NULL,
                (ERROR_NO_FILE, "MatrixRead(%s) - file open failed\n", fname));


  if (!fgets(line, 399, fp))
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixRead: could not read 1st line from %s\n", fname));

  for (cols = 0,cp = strtok(line, " \t,") ; cp ; cp = strtok(NULL, " \t,"))
    cols++ ;

  for (rows = 1 ; fgets(line, 399, fp) ; rows++)
  {}

  mat = MatrixAlloc(rows, cols, MATRIX_REAL) ;
  rewind(fp) ;

  for (row = 1 ; fgets(line, 399, fp) ; row++)
  {
    for (col = 1, cp = strtok(line, " \t,") ;
          cp ;
          cp = strtok(NULL, " \t,"),
          col++)
    {
      if (sscanf(cp, "%f", &mat->rptr[row][col]) != 1)
      {
        MatrixFree(&mat) ;
        ErrorReturn(NULL,
                    (ERROR_BADPARM, 
                     "MatrixRead: could not scan value [%d][%d]\n", row, col));
      }
    }
  }

  fclose(fp) ;
  return(mat) ;
}

#include "matfile.h"
MATRIX *
MatrixRead(char *fname)
{
  return(MatlabRead(fname)) ;
}

int
MatrixWrite(MATRIX *mat, char *fname, char *name)
{
  if (!name)
    name = fname ;  /* name of matrix in .mat file */
  return(MatlabWrite(mat, fname, name)) ;
}

MATRIX *
MatrixIdentity(int n, MATRIX *mat)
{
  int     i ;

  if (!mat)
    mat = MatrixAlloc(n, n, MATRIX_REAL) ;
  else
    MatrixClear(mat) ;

  for (i = 1 ; i <= n ; i++)
    mat->rptr[i][i] = 1 ;

  return(mat) ;
}

MATRIX  *
MatrixTranspose(MATRIX *mIn, MATRIX *mOut)
{
  int  row, col, rows, cols ;

  if (!mOut)
  {
    mOut = MatrixAlloc(mIn->cols, mIn->rows, mIn->type) ;
    if (!mOut)
      return(NULL) ;
  }

  rows = mIn->rows ;
  cols = mIn->cols ;

  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
      mOut->rptr[col][row] = mIn->rptr[row][col] ;
  }

  return(mOut) ;
}

MATRIX  *
MatrixAdd(MATRIX *m1, MATRIX *m2, MATRIX *mOut)
{
  int  row, col, rows, cols ;

  rows = m1->rows ;
  cols = m1->cols ;

  if ((rows != m2->rows) || (cols != m2->cols))
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixAdd: incompatable matrices %d x %d + %d x %d\n",
                 rows, cols, m2->rows, m2->cols)) ;

  if (!mOut)
  {
    mOut = MatrixAlloc(rows, cols, m1->type) ;
    if (!mOut)
      return(NULL) ;
  }

  if ((rows != mOut->rows) || (cols != mOut->cols))
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixAdd: incompatable matrices %d x %d = %d x %d\n",
                 rows, cols, mOut->rows, mOut->cols)) ;

  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      mOut->rptr[row][col] = m1->rptr[row][col] + m2->rptr[row][col] ;
    }
  }

  return(mOut) ;
}

MATRIX  *
MatrixSubtract(MATRIX *m1, MATRIX *m2, MATRIX *mOut)
{
  int  row, col, rows, cols ;

  rows = m1->rows ;
  cols = m1->cols ;

  if ((rows != m2->rows) || (cols != m2->cols))
    ErrorReturn(NULL,
                (ERROR_BADPARM,
                 "MatrixSubtract: incompatable matrices %d x %d - %d x %d\n",
                 rows, cols, m2->rows, m2->cols)) ;

  if (!mOut)
  {
    mOut = MatrixAlloc(rows, cols, m1->type) ;
    if (!mOut)
      return(NULL) ;
  }

  if ((rows != mOut->rows) || (cols != mOut->cols))
    ErrorReturn(NULL,
                (ERROR_BADPARM,
                 "MatrixSubtract: incompatable matrices %d x %d = %d x %d\n",
                 rows, cols, mOut->rows, mOut->cols)) ;

  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
      mOut->rptr[row][col] = m1->rptr[row][col] - m2->rptr[row][col] ;
  }

  return(mOut) ;
}


MATRIX  *
MatrixScalarMul(MATRIX *mIn, float val, MATRIX *mOut)
{
  int  row, col, rows, cols ;

  if (!mOut)
  {
    mOut = MatrixAlloc(mIn->rows, mIn->cols, mIn->type) ;
    if (!mOut)
      return(NULL) ;
  }

  rows = mIn->rows ;
  cols = mIn->cols ;

  if ((rows != mOut->rows) || (cols != mOut->cols))
    ErrorReturn(NULL,
                (ERROR_BADPARM,
                 "MatrixScalarMul: incompatable matrices %d x %d != %d x %d\n",
                 rows, cols, mOut->rows, mOut->cols)) ;

  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
      mOut->rptr[row][col] = mIn->rptr[row][col] * val ;
  }
  return(mOut) ;
}

MATRIX  *
MatrixClear(MATRIX *mat)
{
  int    rows, row, cols ;

  rows = mat->rows ;
  cols = mat->cols ;
  for (row = 1 ; row <= mat->rows ; row++)
    memset((char *)mat->rptr[row], 0, (cols+1) * sizeof(float)) ;

  return(mat) ;
}

MATRIX *
MatrixSquareElts(MATRIX *mIn, MATRIX *mOut)
{
  int    row, col, rows, cols ;
  float  val ;

  if (!mOut)
  {
    mOut = MatrixAlloc(mIn->rows, mIn->cols, mIn->type) ;
    if (!mOut)
      return(NULL) ;
  }

  rows = mIn->rows ;
  cols = mIn->cols ;

  if ((rows != mOut->rows) || (cols != mOut->cols))
    ErrorReturn(NULL,
              (ERROR_BADPARM,
               "MatrixSquareElts: incompatable matrices %d x %d != %d x %d\n",
               rows, cols, mOut->rows, mOut->cols)) ;


  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      val = mIn->rptr[row][col] ;
      mOut->rptr[row][col] = val * val ;
    }
  }
  return(mOut) ;
}


MATRIX *
MatrixSqrtElts(MATRIX *mIn, MATRIX *mOut)
{
  int    row, col, rows, cols ;
  float  val ;

  if (!mOut)
  {
    mOut = MatrixAlloc(mIn->rows, mIn->cols, mIn->type) ;
    if (!mOut)
      return(NULL) ;
  }

  rows = mIn->rows ;
  cols = mIn->cols ;

  if ((rows != mOut->rows) || (cols != mOut->cols))
    ErrorReturn(NULL,
              (ERROR_BADPARM,
               "MatrixSquareElts: incompatable matrices %d x %d != %d x %d\n",
               rows, cols, mOut->rows, mOut->cols)) ;


  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      val = mIn->rptr[row][col] ;
      mOut->rptr[row][col] = sqrt(val) ;
    }
  }
  return(mOut) ;
}

MATRIX *
MatrixSignedSquareElts(MATRIX *mIn, MATRIX *mOut)
{
  int    row, col, rows, cols ;
  float  val ;

  if (!mOut)
  {
    mOut = MatrixAlloc(mIn->rows, mIn->cols, mIn->type) ;
    if (!mOut)
      return(NULL) ;
  }

  rows = mIn->rows ;
  cols = mIn->cols ;

  if ((rows != mOut->rows) || (cols != mOut->cols))
    ErrorReturn(NULL,
              (ERROR_BADPARM,
               "MatrixSquareElts: incompatable matrices %d x %d != %d x %d\n",
               rows, cols, mOut->rows, mOut->cols)) ;


  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      val = mIn->rptr[row][col] ;
      mOut->rptr[row][col] = val * val * (val < 0) ? -1 : 1 ;
    }
  }
  return(mOut) ;
}

MATRIX *
MatrixMakeDiagonal(MATRIX *mSrc, MATRIX *mDst)
{
  int  row, rows, col, cols ;

  if (!mDst)
    mDst = MatrixClone(mSrc) ;
  
  rows = mSrc->rows ;
  cols = mSrc->cols ;

  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      if (row == col)
        mDst->rptr[row][col] = mSrc->rptr[row][col] ;
      else
        mDst->rptr[row][col] = 0.0f ;
    }
  }
  return(mDst) ;
}

/*
  mDiag is a column vector.
*/
MATRIX  *
MatrixDiag(MATRIX *mDiag, MATRIX *mOut)
{
  int  row, rows ;

  if (!mOut)
  {
    mOut = MatrixAlloc(mDiag->rows, mDiag->rows, mDiag->type) ;
    if (!mOut)
      return(NULL) ;
  }
  else
    MatrixClear(mOut) ;

  rows = mDiag->rows ;

  if (mDiag->cols != 1)
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixDiag: input matrix must be a column vector.\n")) ;

  if ((rows != mOut->rows) || (rows != mOut->cols))
    ErrorReturn(NULL,
                (ERROR_BADPARM,
                 "MatrixDiag: incompatable matrices %d x %d != %d x %d\n",
                 rows, rows, mOut->rows, mOut->cols)) ;


  for (row = 1 ; row <= rows ; row++)
    mOut->rptr[row][row] = mDiag->rptr[row][1] ;

  return(mOut) ;
}
MATRIX *
MatrixCopyRegion(MATRIX *mSrc, MATRIX *mDst, int start_row, int start_col,
                 int rows, int cols, int dest_row, int dest_col)
{
  int srow, scol, drow, dcol, srows, scols, drows, dcols, end_row, end_col ;

  if ((dest_col < 1) || (dest_row < 1))
    ErrorReturn(NULL,
                (ERROR_BADPARM, "MatrixCopyRegion: bad destination (%d,%d)\n",
                 dest_row, dest_col)) ;

  srows = mSrc->rows ;
  scols = mSrc->cols ;
  end_row = start_row + rows - 1 ;
  end_col = start_col + cols - 1 ;
  if ((start_row < 1) || (start_row > srows) || (start_col < 1) ||
      (start_col > scols) || (end_row > srows) || (end_col > scols))
    ErrorReturn(NULL,
                (ERROR_BADPARM,
                 "MatrixCopyRegion: bad source region (%d,%d) --> (%d,%d)\n",
                 start_row, start_col, end_row, end_col)) ;

  if (!mDst)
    mDst = MatrixAlloc(rows, cols, mSrc->type) ;

  drows = mDst->rows ;
  dcols = mDst->cols ;
  if ((rows > drows) || (cols > dcols))
    ErrorReturn(NULL,
            (ERROR_BADPARM, 
           "MatrixCopyRegion: destination matrix not large enough (%dx%d)\n",
            rows, cols)) ;

  for (drow = dest_row, srow = start_row ; srow <= end_row ; srow++, drow++)
  {
    for (dcol = dest_col, scol = start_col ; scol <= end_col ; scol++, dcol++)
    {
      switch (mDst->type)
      {
      case MATRIX_REAL:
        *MATRIX_RELT(mDst, drow, dcol) = *MATRIX_RELT(mSrc, srow, scol) ;
        break ;
      case MATRIX_COMPLEX:
        *MATRIX_CELT(mDst, drow, dcol) = *MATRIX_CELT(mSrc,srow,scol);
        break ;
      }
    }
  }
  return(mDst) ;
}

MATRIX *
MatrixCopyRealRegion(MATRIX *mSrc, MATRIX *mDst, int start_row, int start_col,
                 int rows, int cols, int dest_row, int dest_col)
{
  int srow, scol, drow, dcol, srows, scols, drows, dcols, end_row, end_col ;

  if ((dest_col < 1) || (dest_row < 1))
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixCopyRealRegion: bad destination (%d,%d)\n",
                 dest_row, dest_col)) ;

  srows = mSrc->rows ;
  scols = mSrc->cols ;
  end_row = start_row + rows - 1 ;
  end_col = start_col + cols - 1 ;
  if ((start_row < 1) || (start_row > srows) || (start_col < 1) ||
      (start_col > scols) || (end_row > srows) || (end_col > scols))
    ErrorReturn(NULL,
            (ERROR_BADPARM,
             "MatrixCopyRealRegion: bad source region (%d,%d) --> (%d,%d)\n",
             start_row, start_col, end_row, end_col)) ;

  if (!mDst)
    mDst = MatrixAlloc(rows, cols, mSrc->type) ;

  drows = mDst->rows ;
  dcols = mDst->cols ;
  if ((rows > drows) || (cols > dcols))
    ErrorReturn(NULL,
          (ERROR_BADPARM, 
         "MatrixCopyRealRegion: destination matrix not large enough (%dx%d)\n",
           rows, cols)) ;

  for (drow = dest_row, srow = start_row ; srow <= end_row ; srow++, drow++)
  {
    for (dcol = dest_col, scol = start_col ; scol <= end_col ; scol++, dcol++)
    {
      switch (mDst->type)
      {
      case MATRIX_REAL:
        *MATRIX_RELT(mDst, drow, dcol) = MATRIX_CELT_REAL(mSrc, srow, scol) ;
        break ;
      case MATRIX_COMPLEX:
        MATRIX_CELT_IMAG(mDst, drow, dcol) = MATRIX_CELT_IMAG(mSrc,srow,scol);
        break ;
      }
    }
  }
  return(mDst) ;
}


MATRIX *
MatrixCopyImagRegion(MATRIX *mSrc, MATRIX *mDst, int start_row, int start_col,
                 int rows, int cols, int dest_row, int dest_col)
{
  int srow, scol, drow, dcol, srows, scols, drows, dcols, end_row, end_col ;

  if ((dest_col < 1) || (dest_row < 1))
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixCopyImagRegion: bad destination (%d,%d)\n",
                 dest_row, dest_col)) ;

  srows = mSrc->rows ;
  scols = mSrc->cols ;
  end_row = start_row + rows - 1 ;
  end_col = start_col + cols - 1 ;
  if ((start_row < 1) || (start_row > srows) || (start_col < 1) ||
      (start_col > scols) || (end_row > srows) || (end_col > scols))
    ErrorReturn(NULL,
              (ERROR_BADPARM,
               "MatrixCopyImagRegion: bad source region (%d,%d) --> (%d,%d)\n",
               start_row, start_col, end_row, end_col)) ;

  if (!mDst)
    mDst = MatrixAlloc(rows, cols, mSrc->type) ;

  drows = mDst->rows ;
  dcols = mDst->cols ;
  if ((rows > drows) || (cols > dcols))
    ErrorReturn(NULL,
          (ERROR_BADPARM, 
         "MatrixCopyImagRegion: destination matrix not large enough (%dx%d)\n",
           rows, cols)) ;

  for (drow = dest_row, srow = start_row ; srow <= end_row ; srow++, drow++)
  {
    for (dcol = dest_col, scol = start_col ; scol <= end_col ; scol++, dcol++)
    {
      switch (mDst->type)
      {
      case MATRIX_REAL:
        *MATRIX_RELT(mDst, drow, dcol) = MATRIX_CELT_IMAG(mSrc, srow, scol) ;
        break ;
      case MATRIX_COMPLEX:
        MATRIX_CELT_IMAG(mDst, drow, dcol) = MATRIX_CELT_IMAG(mSrc,srow,scol);
        break ;
      }
    }
  }
  return(mDst) ;
}


MATRIX *
MatrixRealToComplex(MATRIX *mReal, MATRIX *mImag, MATRIX *mOut)
{
  int rows, cols, row, col ;

  rows = mReal->rows ;
  cols = mReal->cols ;
  if (!mOut)
    mOut = MatrixAlloc(mReal->rows, mReal->cols, MATRIX_COMPLEX) ;

  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      MATRIX_CELT_REAL(mOut,row,col) = *MATRIX_RELT(mReal, row, col) ;
      MATRIX_CELT_IMAG(mOut,row,col) = *MATRIX_RELT(mImag, row, col) ;
    }
  }

  return(mOut) ;
}

float
MatrixDeterminant(MATRIX *mIn)
{
  float  d, **in ;
  int    j,*index, rows ;
  MATRIX *mSrc ;

  rows = mIn->rows ;
  mSrc = MatrixCopy(mIn, NULL) ;
  in = mSrc->rptr ;
  index = (int *)calloc(rows+1, sizeof(int)) ;
  if (ludcmp(in, rows, index, &d) < 0)
    return(0.0f) ;

  for (j = 1 ; j <= rows ; j++) 
    d *= in[j][j];

  free(index) ;
  MatrixFree(&mSrc) ;
  return d;
}

/* return the eigenvalues and eigenvectors of the symmetric matrix m in
   evalues and m_dst (columns are vectors) respectively. Note that
   the eigenvalues and eigenvectors are sorted in descending order of
   eigenvalue size.
*/

static int compare_evalues(const void *l1, const void *l2)  ;


typedef struct
{
  int   eno ;
  float evalue ;
} EIGEN_VALUE, EVALUE ;

static int
compare_evalues(const void *l1, const void *l2)
{
  EVALUE *e1, *e2 ;

  e1 = (EVALUE *)l1 ;
  e2 = (EVALUE *)l2 ;
  return(fabs(e1->evalue) < fabs(e2->evalue) ? 1 : -1) ;
}

MATRIX *
MatrixEigenSystem(MATRIX *m, float *evalues, MATRIX *m_evectors)
{
  int     col, i, nevalues, row ;
  EVALUE  *eigen_values ;
  MATRIX  *mTmp ;

  nevalues = m->rows ;
  eigen_values = (EVALUE *)calloc((UINT)nevalues, sizeof(EIGEN_VALUE));
  if (!m_evectors)
    m_evectors = MatrixAlloc(m->rows, m->cols, MATRIX_REAL) ;

  mTmp = MatrixAlloc(m->rows, m->cols, MATRIX_REAL) ;
  if (EigenSystem(m->data, m->rows, evalues, mTmp->data) != NO_ERROR)
    return(NULL) ;

/* 
  sort eigenvalues in order of decreasing absolute value. The
  EIGEN_VALUE structure is needed to record final order of eigenvalue so 
  we can also sort the eigenvectors.
*/
  for (i = 0 ; i < nevalues ; i++)
  {
    eigen_values[i].eno = i ;
    eigen_values[i].evalue = evalues[i] ;
  }
  qsort((char *)eigen_values, nevalues, sizeof(EVALUE), compare_evalues) ;
  for (i = 0 ; i < nevalues ; i++)
    evalues[i] = eigen_values[i].evalue ;

  /* now sort the eigenvectors */
  for (col = 0 ; col < mTmp->cols ; col++)
  {
    for (row = 1 ; row <= mTmp->rows ; row++)
      m_evectors->rptr[row][col+1] = mTmp->rptr[row][eigen_values[col].eno+1] ;
  }

  free(eigen_values) ;
  MatrixFree(&mTmp) ;
  return(m_evectors) ;
}


#include "nrutil.h"

void identity_matrix(float **I,int n) ;
void svd(float **A, float **V, float *z, int m, int n) ;

/* z is an m->cols dimensional vector, mV is an cols x cols dimensional
   identity matrix. Note that all the matrices are (as usual) one based.
*/
MATRIX *
MatrixSVD(MATRIX *mA, VECTOR *v_z, MATRIX *mV)
{
  mV = MatrixIdentity(mA->rows, mV) ;

  svd(mA->rptr, mV->rptr, v_z->data, mA->rows, mA->cols) ;
  return(mV) ;
}
float
MatrixSVDEigenValues(MATRIX *m, float *evalues)
{
  float cond ;
  VECTOR  *v_w ;
  MATRIX  *m_U, *m_V ;
  int     row, rows, cols, nevalues, i ;
  float   wmax, wmin, wi ;
  EVALUE  *eigen_values ;

  cols = m->cols ;
  rows = m->rows ;
  m_U = MatrixCopy(m, NULL) ;
  v_w = RVectorAlloc(cols, MATRIX_REAL) ;
  m_V = MatrixAlloc(cols, cols, MATRIX_REAL) ;
  nevalues = m->rows ;
  memset(evalues, 0, nevalues*sizeof(evalues[0])) ;
  
  /* calculate condition # of matrix */
  if (svdcmp(m_U->rptr, rows, cols, v_w->rptr[1], m_V->rptr) != NO_ERROR)
    return(Gerror) ;

  eigen_values = (EVALUE *)calloc((UINT)nevalues, sizeof(EIGEN_VALUE));
  for (i = 0 ; i < nevalues ; i++)
  {
    eigen_values[i].eno = i ;
    eigen_values[i].evalue = RVECTOR_ELT(v_w, i+1) ;
  }
  qsort((char *)eigen_values, nevalues, sizeof(EVALUE), compare_evalues) ;
  for (i = 0 ; i < nevalues ; i++)
    evalues[i] = eigen_values[i].evalue ;

  wmax = 0.0f ;
  wmin = wmax = RVECTOR_ELT(v_w,1) ;
  for (row = 2 ; row <= rows ; row++)
  {
    wi = fabs(RVECTOR_ELT(v_w,row)) ;
    if (wi > wmax)
      wmax = wi ;
    if (wi < wmin)
      wmin = wi ;
  }

  if (FZERO(wmin))
    cond = 1e8 ;  /* something big */
  else
    cond = wmax / wmin ;

  free(eigen_values) ;
  MatrixFree(&m_U) ;
  VectorFree(&v_w) ;
  MatrixFree(&m_V) ;
  return(cond) ;
}
/*
   use SVD to find the inverse of m, usually useful if m is 
   ill-conditioned or singular.
*/
#define TOO_SMALL   1e-4

MATRIX *
MatrixSVDInverse(MATRIX *m, MATRIX *m_inverse)
{
  VECTOR  *v_w ;
  MATRIX  *m_U, *m_V, *m_w, *m_Ut, *m_tmp ;
  int     row, rows, cols ;
  float   wmax, wmin ;

  cols = m->cols ;
  rows = m->rows ;
  m_U = MatrixCopy(m, NULL) ;
  v_w = RVectorAlloc(cols, MATRIX_REAL) ;
  m_V = MatrixAlloc(cols, cols, MATRIX_REAL) ;
  m_w = MatrixAlloc(cols, cols, MATRIX_REAL) ;

  if (svdcmp(m_U->rptr, rows, cols, v_w->rptr[1], m_V->rptr) != NO_ERROR)
  {
    MatrixFree(&m_U) ;
    VectorFree(&v_w) ;
    MatrixFree(&m_V) ;
    MatrixFree(&m_w) ;
    return(NULL) ;
  }

  wmax = 0.0f ;
  for (row = 1 ; row <= rows ; row++)
    if (fabs(RVECTOR_ELT(v_w,row)) > wmax)
      wmax = fabs(RVECTOR_ELT(v_w,row)) ;
  wmin = TOO_SMALL * wmax ;
  for (row = 1 ; row <= rows ; row++)
  {
    if (fabs(RVECTOR_ELT(v_w, row)) < wmin)
      m_w->rptr[row][row] = 0.0f ;
    else
      m_w->rptr[row][row] = 1.0f / RVECTOR_ELT(v_w,row) ;
  }

  m_Ut = MatrixTranspose(m_U, NULL) ;
  m_tmp = MatrixMultiply(m_w, m_Ut, NULL) ;
  m_inverse = MatrixMultiply(m_V, m_tmp, m_inverse) ;

  MatrixFree(&m_U) ;
  VectorFree(&v_w) ;
  MatrixFree(&m_V) ;
  MatrixFree(&m_w) ;

  MatrixFree(&m_Ut) ;
  MatrixFree(&m_tmp) ;
  return(m_inverse) ;
}



/* m is rows, and n is cols */
void
svd(float **A, float **V, float *z, int m, int n)
{
  int i,j,k,count;
  float c,s,p,q,r,v,toll=0.1;

  identity_matrix(V,n);
  for (count=n*(n-1)/2;count>0;)
  {
    count=n*(n-1)/2;
    for (j=1;j<=n;j++)
    {
      for (k=j+1;k<=n;k++)
      {
        p=q=r=0;
        for (i=1;i<=m;i++)
        {
          p += A[i][j]*A[i][k];
          q += A[i][j]*A[i][j];
          r += A[i][k]*A[i][k];
        }
        if ((q*r==0)||(p*p/(q*r)<toll)) count--;
        if (q<r)
        {
          c=0;
          s=1;
        } else
        {
          q = q-r;
          v = (float)sqrt((double)(4.0f*p*p+q*q));
          c = (float)sqrt((double)((v+q)/(2.0f*v)));
          s = p/(v*c);
        }
        for (i=1;i<=m;i++)
        {
          r = A[i][j];
          A[i][j] = r*c+A[i][k]*s;
          A[i][k] = -r*s+A[i][k]*c;
        }
        for (i=1;i<=n;i++)
        {
          r = V[i][j];
          V[i][j] = r*c+V[i][k]*s;
          V[i][k] = -r*s+V[i][k]*c;
        }
      }
    }
#if 0
    printf("count=%d\n",count);
    print_matrix(A,m,n);
#endif
  }
  for (j=1;j<=n;j++)
  {
    q = 0;
    for (i=1;i<=m;i++) q += A[i][j]*A[i][j];
    q = sqrt(q);
    z[j-1] = q;

    for (i=1;i<=m;i++) A[i][j] /= q;

  }
}


void
identity_matrix(float **I,int n)
{
  int i,j;

  for (i = 1 ; i <= n ; i++) for (j=1 ; j <= n ; j++) 
    I[i][j] = (i==j) ? 1.0 : 0.0 ;
}

#if 0 
static float at,bt,ct;
#define PYTHAG(a,b) ((at=fabs(a)) > (bt=fabs(b)) ? \
(ct=bt/at,at*sqrt(1.0+ct*ct)) : (bt ? (ct=at/bt,bt*sqrt(1.0+ct*ct)):0.0))

static float maxarg1,maxarg2;
#ifndef MAX
#define MAX(a,b) (maxarg1=(a),maxarg2=(b),(maxarg1) > (maxarg2) ? (maxarg1):(maxarg2))
#endif
#ifndef SIGN
#define SIGN(a,b) ((b) >= 0.0 ? fabs(a) : -fabs(a))
#endif

svdcmp(a,w,v,m,n)
float **a,*w,**v;
int m,n;
{
  int flag,i,its,j,jj,k,l,nm;
  float c,f,h,s,x,y,z;
  float anorm=0.0,g=0.0,scale=0.0;
  float *rv1;

  if (m<n) 
    ErrorExit(ERROR_BADPARM, "Singular Value Decomp: too few rows in A");
  rv1=vector(n);
  for (i=0;i<n;i++)
  {
    l=i+1;
    rv1[i]=scale*g;
    g=s=scale=0.0;
    if (i<m)
    {
      for (k=i;k<m;k++) scale += fabs(a[k][i]);
      if (scale)
      {
        for (k=i;k<m;k++)
        {
          a[k][i] /= scale;
          s += a[k][i]*a[k][i];
        }       
        f=a[i][i];
        g = -SIGN(sqrt(s),f);
        h=f*g-s;
        a[i][i]=f-g;
        if (i != n-1)
        {
          for (j=l;j<n;j++)
          {
            for (s=0.0,k=i;k<m;k++) s += a[k][i]*a[k][j];
            f=s/h;
            for (k=i;k<m;k++) a[k][j] += f*a[k][i];
          }
        }
        for (k=i;k<m;k++) a[k][i] *= scale;
      }
    }
    w[i]=scale*g;
    g=s=scale=0.0;
    if (i < m && i != n-1)
    {
      for (k=l;k<n;k++) scale += fabs(a[i][k]);
      if (scale)
      {
        for (k=l;k<n;k++)
        {
          a[i][k] /= scale;
          s += a[i][k]*a[i][k];
        }
        f=a[i][l];
        g = -SIGN(sqrt(s),f);
        h=f*g-s;
        a[i][l]=f-g;
        for (k=l;k<n;k++) rv1[k]=a[i][k]/h;
        if (i != m-1)
        {
          for (j=l;j<m;j++)
          {
            for (s=0.0,k=l;k<n;k++) s += a[j][k]*a[i][k];
            for (k=l;k<n;k++) a[j][k] += s*rv1[k];
          }
        }
        for (k=l;k<n;k++) a[i][k] *= scale;
      }
    }
    anorm=MAX(anorm,(fabs(w[i])+fabs(rv1[i])));
  }

  for (i=n-1;i>=0;i--) 
  {
    if (i < n-1)
    {
      if (g)
      {
        for (j=l;j<n;j++) v[j][i]=(a[i][j]/a[i][l])/g;
        for (j=l;j<n;j++)
        {
          for (s=0.0,k=l;k<n;k++) s += a[i][k]*v[k][j];
          for (k=l;k<n;k++) v[k][j] += s*v[k][i];
        }
      }
      for (j=l;j<n;j++) v[i][j]=v[j][i]=0.0;
    }
    v[i][i]=1.0;
    g=rv1[i];
    l=i;
  }

  for (i=n-1;i>=0;i--) 
  {
    l=i+1;
    g=w[i];
    if (i < n-1)
      for (j=l;j<n;j++) a[i][j]=0.0;
    if (g)
    {
      g=1.0/g;
      if (i != n-1)
      {
        for (j=l;j<n;j++)
        {
          for (s=0.0,k=l;k<m;k++) s += a[k][i]*a[k][j];
          f=(s/a[i][i])*g;
          for (k=i;k<m;k++) a[k][j] += f*a[k][i];
        }
      }
      for (j=i;j<m;j++) a[j][i] *= g;
    } else
    {
      for (j=i;j<m;j++) a[j][i]=0.0;
    }
    ++a[i][i];
  }

  for (k=n-1;k>=0;k--)
  {
    for (its=1;its<=30;its++)
    {
      flag=1;
      for (l=k;l>=0;l--)
      {
        nm=l-1;
        if ((float)(fabs(rv1[l])+anorm) == anorm)
        {
          flag=0;
          break;
        }
        if ((float)(fabs(w[nm])+anorm) == anorm) break;
      }
      if (flag)
      {
        c=0.0;
        s=1.0;
        for (i=l;i<=k;i++)
        {
          f=s*rv1[i];
          rv1[i]=c*rv1[i];
          if ((float)(fabs(f)+anorm) == anorm) break;
          g=w[i];
          h=PYTHAG(f,g);
          w[i]=h;
          h=1.0/h;
          c=g*h;
          s=(-f*h);
          for (j=0;j<m;j++)
          {
            y=a[j][nm];
            z=a[j][i];
            a[j][nm]=y*c+z*s;
            a[j][i]=z*c-y*s;
          }
        }
      }
      z=w[k];
      if (l==k)
      {
        if (z < 0.0)
        {
          w[k] = -z;
          for (j=0;j<n;j++) v[j][k]=(-v[j][k]);
        }
        break;
      }
      if (its==30) 
        return(Gerror = ERROR_BADPARMS) ;

      x=w[l];
      nm=k-1;
      y=w[nm];
      g=rv1[nm];
      h=rv1[k];
      f=((y-z)*(y+z)+(g-h)*(g+h))/(2.0*h*y);
      g=PYTHAG(f,1.0);
      f=((x-z)*(x+z)+h*((y/(f+SIGN(g,f)))-h))/x;

      c=s=1.0;
      for (j=l;j<=nm;j++)
      {
        i=j+1;
        g=rv1[i];
        y=w[i];
        h=s*g;
        g=c*g;
        z=PYTHAG(f,h);
        rv1[j]=z;
        c=f/z;
        s=h/z;
        f=x*c+g*s;
        g=g*c-x*s;
        h=y*s;
        y=y*c;
        for (jj=0;jj<n;jj++)
        {
          x=v[jj][j];
          z=v[jj][i];
          v[jj][j]=x*c+z*s;
          v[jj][i]=z*c-x*s;
        }
        z=PYTHAG(f,h);
        w[j]=z;
        if (z)
        {
          z=1.0/z;
          c=f*z;
          s=h*z;
        }
        f=(c*g)+(s*y);
        x=(c*y)-(s*g);
        for (jj=0;jj<m;jj++)
        {
          y=a[jj][j];
          z=a[jj][i];
          a[jj][j]=y*c+z*s;
          a[jj][i]=z*c-y*s;
        }
      }
      rv1[l]=0.0;
      rv1[k]=f;
      w[k]=x;
    }
  }
  free(rv1);
}
#endif


MATRIX *
MatrixAllocTranslation(int n, double *trans)
{
  MATRIX *mat ;

  mat = MatrixAlloc(n, n, MATRIX_REAL) ;
  return(mat) ;
}

MATRIX *
MatrixReallocRotation(int n, float angle, int which, MATRIX *m)
{
  float  s, c ;

  if (!m)
    m = MatrixIdentity(n, NULL) ;

  c = cos(angle) ;
  s = sin(angle) ;
  switch (which)
  {
  case X_ROTATION:
    m->rptr[2][2] = c ;
    m->rptr[2][3] = s ;
    m->rptr[3][2] = -s ;
    m->rptr[3][3] = c ;
    break ;
  case Y_ROTATION:
    m->rptr[1][1] = c ;
    m->rptr[1][3] = -s ;
    m->rptr[3][1] = s ;
    m->rptr[3][3] = c ;
    break ;
  case Z_ROTATION:
    m->rptr[1][1] = c ;
    m->rptr[1][2] = s ;
    m->rptr[2][1] = -s ;
    m->rptr[2][2] = c ;
    break ;
  }

  return(m) ;
}
MATRIX *
MatrixAllocRotation(int n, float angle, int which)
{
  return(MatrixReallocRotation(n, angle, which, NULL)) ;
}

MATRIX *
MatrixCovariance(MATRIX *mInputs, MATRIX *mCov, VECTOR *mMeans)
{
  int    ninputs, nvars, input, var, var2 ;
  float  *means, covariance, obs1, obs2 ;

  ninputs = mInputs->rows ;   /* number of observations */
  nvars = mInputs->cols ;   /* number of variables */

  if (!ninputs)
  {
    if (!mCov)
      mCov = MatrixAlloc(nvars, nvars, MATRIX_REAL) ;
    return(mCov) ;
  }

  if (!nvars)
    ErrorReturn(NULL,
                (ERROR_BADPARM, "MatrixCovariance: zero size input")) ;

  if (!mCov)
    mCov = MatrixAlloc(nvars, nvars, MATRIX_REAL) ;

  if (!mCov)
    ErrorExit(ERROR_NO_MEMORY, 
              "MatrixCovariance: could not allocate %d x %d covariance matrix",
              nvars, nvars) ;

  if (mCov->cols != nvars || mCov->rows != nvars)
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixCovariance: incorrect covariance matrix dimensions"));

  if (!mMeans)
    means = (float *)calloc(nvars, sizeof(float)) ;
  else 
    means = mMeans->data ;

  for (var = 0 ; var < nvars ; var++)
  {
    means[var] = 0.0f ;
    for (input = 0 ; input < ninputs ; input++)
      means[var] += mInputs->rptr[input+1][var+1] ;
    means[var] /= ninputs ;
  }

  for (var = 0 ; var < nvars ; var++)
  {
    for (var2 = 0 ; var2 < nvars ; var2++)
    {
      covariance = 0.0f ;
      for (input = 0 ; input < ninputs ; input++)
      {
        obs1 = mInputs->rptr[input+1][var+1] ;
        obs2 = mInputs->rptr[input+1][var2+1] ;
        covariance += (obs1-means[var]) * (obs2 - means[var2]) ;
      }
      covariance /= ninputs ;
      mCov->rptr[var+1][var2+1]= 
        mCov->rptr[var2+1][var+1] = covariance ;
    }
  }

  if (!mMeans)
    free(means) ;
  return(mCov) ;
}

/*
  update the values of a covariance matrix. Note that MatrixFinalCovariance 
  must be called with the means and total # of inputs before the covariance
  matrix is valid.
  */
MATRIX *
MatrixUpdateCovariance(MATRIX *mInputs, MATRIX *mCov, MATRIX *mMeans)
{
  int    ninputs, nvars, input, var, var2 ;
  float  covariance, obs1, obs2, mean1, mean2 ;

  ninputs = mInputs->rows ;   /* number of observations */
  nvars = mInputs->cols ;   /* number of variables */

  if (!mMeans || mMeans->rows != nvars)
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixUpdateCovariance: wrong mean vector size (%d x %d)",
                 mMeans->rows, mMeans->cols)) ;

  if (!ninputs)
  {
    if (!mCov)
      mCov = MatrixAlloc(nvars, nvars, MATRIX_REAL) ;
    return(mCov) ;
  }

  if (!nvars)
    ErrorReturn(NULL,
                (ERROR_BADPARM, "MatrixUpdateCovariance: zero size input")) ;

  if (!mCov)
    mCov = MatrixAlloc(nvars, nvars, MATRIX_REAL) ;

  if (!mCov)
    ErrorExit(ERROR_NO_MEMORY, 
              "MatrixUpdateCovariance: could not allocate %d x %d covariance "
              "matrix",
              nvars, nvars) ;

  if (mCov->cols != nvars || mCov->rows != nvars)
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixUpdateCovariance: incorrect covariance matrix "
                 "dimensions"));

  for (var = 0 ; var < nvars ; var++)
  {
    mean1 = mMeans->rptr[var+1][1] ;
    for (var2 = 0 ; var2 < nvars ; var2++)
    {
      mean2 = mMeans->rptr[var2+1][1] ;
      covariance = 0.0f ;
      for (input = 0 ; input < ninputs ; input++)
      {
        obs1 = mInputs->rptr[input+1][var+1] ;
        obs2 = mInputs->rptr[input+1][var2+1] ;
        covariance += (obs1-mean1) * (obs2 - mean2) ;
      }

      mCov->rptr[var+1][var2+1]= 
        mCov->rptr[var2+1][var+1] = covariance ;
    }
  }

  return(mCov) ;
}

/*
  update the means based on a new set of observation vectors. Notet that
  the user must keep track of the total # observations
*/
MATRIX *
MatrixUpdateMeans(MATRIX *mInputs, MATRIX *mMeans, VECTOR *mNobs)
{
  int    ninputs, nvars, input, var ;
  float  *means ;
  
  ninputs = mInputs->rows ;   /* number of observations */
  nvars = mInputs->cols ;     /* number of variables */
  if (!mMeans)
    mMeans = VectorAlloc(nvars, MATRIX_REAL) ;
  means = mMeans->data ;
  for (var = 0 ; var < nvars ; var++)
  {
    mNobs->rptr[var+1][1] += ninputs ;
    for (input = 0 ; input < ninputs ; input++)
      means[var] += mInputs->rptr[input+1][var+1] ;
  }
  return(mMeans) ;
}
/*
  compute the final mean vector previously generated by
  MatrixUpdateMeans. The user must supply a vector containing
  the # of observations of each variable.
*/
MATRIX *
MatrixFinalMeans(VECTOR *mMeans, VECTOR *mNobs)
{
  int    nvars, var, n ;
  float  *means ;
  
  nvars = mMeans->rows ;     /* number of variables */
  means = mMeans->data ;
  for (var = 0 ; var < nvars ; var++)
  {
    n = mNobs->rptr[var+1][1] ;
    if (!n)
      means[var] = 0.0f ;
    else
      means[var] /= (float)n ;
  }
  return(mMeans) ;
}

/*
  compute the final covariance matrix previously generated by 
  MatrixUpdateCovariance. The user must supply a vector containing
  the # of observations per variable.
*/
MATRIX *
MatrixFinalCovariance(MATRIX *mInputs, MATRIX *mCov, VECTOR *mNobs)
{
  int    ninputs, nvars, var, var2 ;
  float  covariance ;

  ninputs = mInputs->rows ;   /* number of observations */
  nvars = mInputs->cols ;   /* number of variables */

  if (!ninputs)
  {
    if (!mCov)
      mCov = MatrixAlloc(nvars, nvars, MATRIX_REAL) ;
    return(mCov) ;
  }

  if (!nvars)
    ErrorReturn(NULL,
                (ERROR_BADPARM, "MatrixCovariance: zero size input")) ;

  if (!mCov)
    mCov = MatrixAlloc(nvars, nvars, MATRIX_REAL) ;

  if (!mCov)
    ErrorExit(ERROR_NO_MEMORY, 
              "MatrixCovariance: could not allocate %d x %d covariance matrix",
              nvars, nvars) ;

  if (mCov->cols != nvars || mCov->rows != nvars)
    ErrorReturn(NULL,
                (ERROR_BADPARM, 
                 "MatrixCovariance: incorrect covariance matrix dimensions"));

  for (var = 0 ; var < nvars ; var++)
  {
    ninputs = mCov->rptr[var+1][1] ;
    for (var2 = 0 ; var2 < nvars ; var2++)
    {
      covariance = mCov->rptr[var+1][var2+1] ;
      if (ninputs)
        covariance /= ninputs ;
      else
        covariance = 0.0f ;
      mCov->rptr[var+1][var2+1] = 
        mCov->rptr[var2+1][var+1] = covariance ;
    }
  }

  return(mCov) ;
}
int
MatrixAsciiWrite(char *fname, MATRIX *m)
{
  FILE  *fp ;
  int   ret ;

  fp = fopen(fname, "w") ;
  if (!fp)
    ErrorReturn(ERROR_NO_FILE, 
                (ERROR_NO_FILE, "MatrixAsciiWrite: could not open file %s",
                 fname)) ;
  ret = MatrixAsciiWriteInto(fp, m) ;
  fclose(fp) ;
  return(ret) ;
}

MATRIX *
MatrixAsciiRead(char *fname, MATRIX *m)
{
  FILE  *fp ;

  fp = fopen(fname, "r") ;
  if (!fp)
    ErrorReturn(NULL, 
                (ERROR_NO_FILE, "MatrixAsciiRead: could not open file %s",
                 fname)) ;
  m = MatrixAsciiReadFrom(fp, m) ;
  fclose(fp) ;
  return(m) ;
}

int
MatrixAsciiWriteInto(FILE *fp, MATRIX *m)
{
  int row, col ;

  fprintf(fp, "%d %d %d\n", m->type, m->rows, m->cols) ;
  for (row = 1 ; row <= m->rows ; row++)
  {
    for (col = 1 ; col <= m->cols ; col++)
    {
      if (m->type == MATRIX_COMPLEX)
        fprintf(fp, "%+f %+f   ", 
                MATRIX_CELT_REAL(m,row,col), MATRIX_CELT_IMAG(m,row,col));
      else
        fprintf(fp, "%+f  ", m->rptr[row][col]) ;
    }
    fprintf(fp, "\n") ;
  }
  return(NO_ERROR) ;
}
MATRIX *
MatrixAsciiReadFrom(FILE *fp, MATRIX *m)
{
  int    type, rows, cols, row, col ;
  char   *cp, line[200] ;

  cp = fgetl(line, 199, fp) ;
  if (!cp)
    ErrorReturn(NULL,
              (ERROR_BADFILE, "MatrixAsciiReadFrom: could not scanf parms"));
  if (sscanf(cp, "%d %d %d\n", &type, &rows, &cols) != 3)
    ErrorReturn(NULL,
              (ERROR_BADFILE, "MatrixAsciiReadFrom: could not scanf parms"));

  if (!m)
  {
    m = MatrixAlloc(rows, cols, type) ;
    if (!m)
      ErrorReturn(NULL,
                  (ERROR_BADFILE, 
                   "MatrixAsciiReadFrom: could not allocate matrix")) ;
  }
  else
  {
    if (m->rows != rows || m->cols != cols || m->type != type)
      ErrorReturn(m,
                (ERROR_BADFILE, 
               "MatrixAsciiReadFrom: specified matrix does not match file"));
  }
  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
    {
      if (m->type == MATRIX_COMPLEX)
      {
        if (fscanf(fp, "%f %f   ", &MATRIX_CELT_REAL(m,row,col), 
                   &MATRIX_CELT_IMAG(m,row,col)) != 2)
          ErrorReturn(NULL,
                      (ERROR_BADFILE, 
                      "MatrixAsciiReadFrom: could not scan element (%d, %d)",
                       row, col)) ;     
      }
      else if (fscanf(fp, "%f  ", &m->rptr[row][col]) != 1)
        ErrorReturn(NULL,
                    (ERROR_BADFILE, 
                     "MatrixAsciiReadFrom: could not scan element (%d, %d)",
                     row, col)) ;
    }
    fscanf(fp, "\n") ;
  }

  return(m) ;
}
/*
   calculate and return the Euclidean norm of the vector v.
*/
float
VectorLen(VECTOR *v)
{
  int   i ;
  float len, vi ;

  for (len = 0.0f, i = 1 ; i <= v->rows ; i++)
  {
    vi = v->rptr[i][1] ;
    len += vi*vi ;
  }
  len = sqrt(len) ;
  return(len) ;
}

/*  compute the dot product of 2 vectors */
float
VectorDot(VECTOR *v1, VECTOR *v2)
{
  int   i ;
  float dot ;

  for (dot = 0.0f, i = 1 ; i <= v1->rows ; i++)
    dot += v1->rptr[i][1]*v2->rptr[i][1] ; ;

  return(dot) ;
}
/*  compute the dot product of 2 vectors */
float
VectorNormalizedDot(VECTOR *v1, VECTOR *v2)
{
  float   dot, l1, l2 ;

  l1 = VectorLen(v1) ;
  l2 = VectorLen(v2) ;
  dot = VectorDot(v1, v2) ;
  if (FZERO(l1) || FZERO(l2))
    return(0.0f) ;

  return(dot / (l1 * l2)) ;
}

/*
   extract a column of the matrix m and return it in the
   vector v.
*/
VECTOR *
MatrixColumn(MATRIX *m, VECTOR *v, int col)
{
  int row ;

  if (!v)
    v = VectorAlloc(m->rows, MATRIX_REAL) ;

  for (row = 1 ; row <= m->rows ; row++)
    VECTOR_ELT(v,row) = m->rptr[row][col] ;
  return(v) ;
}

/* calcuate and return the Euclidean distance between two vectors */
float
VectorDistance(VECTOR *v1, VECTOR *v2)
{
  int   row ;
  float dist, d ;

  for (dist = 0.0f,row = 1 ; row <= v1->rows ; row++)
  {
    d = VECTOR_ELT(v1,row) - VECTOR_ELT(v2,row) ;
    dist += (d*d) ;
  }

  return(dist) ;
}

/*
  compute the outer product of the vectors v1 and v2.
  */
MATRIX *
VectorOuterProduct(VECTOR *v1, VECTOR *v2, MATRIX *m)
{
  int   row, col, rows, cols ;
  float r ;

  rows = v1->rows ;
  cols = v2->rows ;

  if (rows != cols)
    ErrorReturn(NULL, (ERROR_BADPARM, 
                       "VectorOuterProduct: v1->rows %d != v2->rows %d", 
                       rows, cols)) ;

  if (!m)
    m = MatrixAlloc(rows, cols, MATRIX_REAL) ;

  for (row = 1 ; row <= rows ; row++)
  {
    r = VECTOR_ELT(v1,row) ;
    for (col = 1 ; col <= cols ; col++)
      m->rptr[row][col] = r * VECTOR_ELT(v2,col) ;
  }

  return(m) ;
}

/*
   Add a small random diagonal matrix to mIn to make it
   non-singular.
*/
#define SMALL 1e-4
MATRIX *
MatrixRegularize(MATRIX *mIn, MATRIX *mOut)
{
  int   rows, cols, row ;
  float ran_num ;

  rows = mIn->rows ;
  cols = mIn->cols ;
  if (!mOut)
    mOut = MatrixAlloc(rows, cols, mIn->type) ;

  if (mIn != mOut)
    MatrixCopy(mIn, mOut) ;

#if 0
  ran_num = (SMALL*randomNumber(0.0, 1.0)+SMALL) ;
#else
  ran_num = SMALL ;
#endif

  for (row = 1 ; row <= rows ; row++)
    mOut->rptr[row][row] += ran_num ;

  return(mOut) ;
}

/* see if a matrix is singular */
int    
MatrixSingular(MATRIX *m)
{
#if 1
  VECTOR  *v_w ;
  MATRIX  *m_U, *m_V ;
  int     row, rows, cols ;
  float   wmax, wmin, wi ;

  cols = m->cols ;
  rows = m->rows ;
  m_U = MatrixCopy(m, NULL) ;
  v_w = RVectorAlloc(cols, MATRIX_REAL) ;
  m_V = MatrixAlloc(cols, cols, MATRIX_REAL) ;

  /* calculate condition # of matrix */
  if (svdcmp(m_U->rptr, rows, cols, v_w->rptr[1], m_V->rptr) != NO_ERROR)
    return(Gerror) ;

  wmax = 0.0f ;
  wmin = wmax = RVECTOR_ELT(v_w,1) ;
  for (row = 2 ; row <= rows ; row++)
  {
    wi = fabs(RVECTOR_ELT(v_w,row)) ;
    if (wi > wmax)
      wmax = wi ;
    if (wi < wmin)
      wmin = wi ;
  }

  MatrixFree(&m_U) ;
  VectorFree(&v_w) ;
  MatrixFree(&m_V) ;
  return(FZERO(wmin) ? 1 : wmin < wmax * TOO_SMALL) ;
#else
  float det ;

  det = MatrixDeterminant(m) ;
  return(FZERO(det)) ;
#endif
}

/*
   calcluate the condition # of a matrix using svd
*/
float
MatrixConditionNumber(MATRIX *m)
{
  float cond ;
  VECTOR  *v_w ;
  MATRIX  *m_U, *m_V ;
  int     row, rows, cols ;
  float   wmax, wmin, wi ;

  cols = m->cols ;
  rows = m->rows ;
  m_U = MatrixCopy(m, NULL) ;
  v_w = RVectorAlloc(cols, MATRIX_REAL) ;
  m_V = MatrixAlloc(cols, cols, MATRIX_REAL) ;

  /* calculate condition # of matrix */
  svdcmp(m_U->rptr, rows, cols, v_w->rptr[1], m_V->rptr) ;
  wmax = 0.0f ;
  wmin = wmax = RVECTOR_ELT(v_w,1) ;
  for (row = 2 ; row <= rows ; row++)
  {
    wi = fabs(RVECTOR_ELT(v_w,row)) ;
    if (wi > wmax)
      wmax = wi ;
    if (wi < wmin)
      wmin = wi ;
  }

  if (FZERO(wmin))
    cond = 1e8 ;   /* something big */
  else
    cond = wmax / wmin ;

  MatrixFree(&m_U) ;
  VectorFree(&v_w) ;
  MatrixFree(&m_V) ;
  return(cond) ;
}
/*
  calculate the cross product of two vectors and return the result
  in vdst, allocating it if necessary.
  */
VECTOR *
VectorCrossProduct(VECTOR *v1, VECTOR *v2, VECTOR *vdst)
{
  float   x1, x2, y1, y2, z1, z2 ;

  if (v1->rows != 3 && v1->cols != 1)
    ErrorReturn(NULL,(ERROR_BADPARM, "VectorCrossProduct: must be 3-vectors"));

  if (!vdst)
    vdst = VectorClone(v1) ;

  x1 = VECTOR_ELT(v1,1) ; y1 = VECTOR_ELT(v1,2) ; z1 = VECTOR_ELT(v1,3) ;
  x2 = VECTOR_ELT(v2,1) ; y2 = VECTOR_ELT(v2,2) ; z2 = VECTOR_ELT(v2,3) ;

  VECTOR_ELT(vdst,1) = y1*z2 - z1*y2 ;
  VECTOR_ELT(vdst,2) = z1*x2 - x1*z2 ;
  VECTOR_ELT(vdst,3) = x1*y2 - y1*x2 ;
  return(vdst) ;
}

/*
  compute the triple scalar product v1 x v2 . v3
  */
float
VectorTripleProduct(VECTOR *v1, VECTOR *v2, VECTOR *v3)
{
  float   x1, x2, y1, y2, z1, z2, x3, y3, z3, total ;

  if (v1->rows != 3 && v1->cols != 1)
    ErrorReturn(0.0f,(ERROR_BADPARM, "VectorCrossProduct: must be 3-vectors"));

  x1 = VECTOR_ELT(v1,1) ; y1 = VECTOR_ELT(v1,2) ; z1 = VECTOR_ELT(v1,3) ;
  x2 = VECTOR_ELT(v2,1) ; y2 = VECTOR_ELT(v2,2) ; z2 = VECTOR_ELT(v2,3) ;
  x3 = VECTOR_ELT(v3,1) ; y3 = VECTOR_ELT(v3,2) ; z3 = VECTOR_ELT(v3,3) ;

  total =  x3 * (y1*z2 - z1*y2) ;
  total += y3 * (z1*x2 - x1*z2) ;
  total += z3 * (x1*y2 - y1*x2) ;
  return(total) ;
}

VECTOR *
VectorNormalize(VECTOR *vin, VECTOR *vout)
{
  float  len ;
  int    row, col, rows, cols ;

  if (!vout)
    vout = VectorClone(vin) ;

  len = VectorLen(vin) ;
  if (FZERO(len))
    len = 1.0f ;   /* doesn't matter - all elements are 0 */

  rows = vin->rows ; cols = vin->cols ;
  for (row = 1 ; row <= rows ; row++)
  {
    for (col = 1 ; col <= cols ; col++)
      vout->rptr[row][col] = vin->rptr[row][col] / len ;
  }
  return(vout) ;
}
float
VectorAngle(VECTOR *v1, VECTOR *v2)
{
  float  angle, l1, l2, dot, norm ;

  l1 = VectorLen(v1) ;
  l2 = VectorLen(v2) ;
  norm = fabs(l1*l2) ;
  if (FZERO(norm))
    return(0.0f) ;
  dot = VectorDot(v1, v2) ;
  if (dot > norm)
    angle = acos(1.0) ;
  else
    angle = acos(dot / norm) ;
  return(angle) ;
}

double
Vector3Angle(VECTOR *v1, VECTOR *v2)
{
  double  angle, l1, l2, dot, norm, x, y, z ;

  x = V3_X(v1) ; y = V3_Y(v1) ; z = V3_Z(v1) ; l1 = sqrt(x*x+y*y+z*z) ;
  x = V3_X(v2) ; y = V3_Y(v2) ; z = V3_Z(v2) ; l2 = sqrt(x*x+y*y+z*z) ;
  norm = l1*l2 ;
  if (FZERO(norm))
    return(0.0f) ;
  dot = V3_DOT(v1, v2) ;
  if (dot > norm)
    angle = acos(1.0) ;
  else
    angle = acos(dot / norm) ;
  return(angle) ;
}

int
MatrixWriteTxt(char *fname, MATRIX *mat)
{
  FILE   *fp ;
  int     row, col ;

  fp = fopen(fname, "w") ;
  if (!fp)
    ErrorReturn(ERROR_NO_FILE,
                (ERROR_NO_FILE, "MatrixWriteTxt(%s) - file open failed\n", 
                 fname));


  for (row = 1 ; row <= mat->rows ; row++)
  {
    for (col = 1 ; col <= mat->cols ; col++)
      fprintf(fp, "%+4.5f ", mat->rptr[row][col]) ;
    fprintf(fp, "\n") ;
  }

  fclose(fp) ;
  return(NO_ERROR) ;
}

MATRIX *
MatrixPseudoInverse(MATRIX *m, MATRIX *m_pseudo_inv)
{
  MATRIX  *mT, *mTm, *mTm_inv ;

  /* build (mT m)-1 mT */
  mT = MatrixTranspose(m, NULL) ;
  mTm = MatrixMultiply(mT, m, NULL) ;
  mTm_inv = MatrixInverse(mTm, NULL) ;
  if (!mTm_inv)
  {
    MatrixFree(&mT) ; MatrixFree(&mTm) ;
    return(NULL) ;
  }
  m_pseudo_inv = MatrixMultiply(mTm_inv, mT, m_pseudo_inv) ;

  MatrixFree(&mT) ; MatrixFree(&mTm) ; MatrixFree(&mTm_inv) ; 
  return(m_pseudo_inv) ;
}

int
MatrixCheck(MATRIX *m)
{
  int  rows,  cols, r, c ;

  rows = m->rows ; cols = m->cols ;
  for (r = 1 ; r <= rows ; r++)
  {
    for (c = 1 ; c <= cols ; c++)
    {
      if (!finite(*MATRIX_RELT(m, r, c)))
        return(ERROR_BADPARM) ;
    }
  }
  return(NO_ERROR) ;
}

MATRIX  *
MatrixReshape(MATRIX *m_src, MATRIX *m_dst, int rows, int cols)
{
  int   r1, c1, r2, c2 ;

  if (m_dst)
  {
    rows = m_dst->rows ; cols = m_dst->cols ;
  }

#if 0
  if (rows*cols > m_src->rows*m_src->cols)
    ErrorReturn(NULL, (ERROR_BADPARM, 
                       "MatrixReshape: (%d,%d) -> (%d,%d), lengths must be"
                       " equal", m_src->rows, m_src->cols, rows,cols)) ;
#endif
  if (!m_dst)
    m_dst = MatrixAlloc(rows, cols, m_src->type) ;

  for (r2 = c2 = r1 = 1 ; r1 <= m_src->rows ; r1++)
  {
    for (c1 = 1 ; c1 <= m_src->cols ; c1++)
    {
      *MATRIX_RELT(m_dst, r2, c2) = *MATRIX_RELT(m_src, r1, c1) ;
      if (++c2 > m_dst->cols)
      {
        c2 = 1 ;
        r2++ ;
      }
      if (r2 > rows)  /* only extract first rowsxcols elements */
        break ;
    }
    if (r2 > rows)
      break ;
  }


  
  return(m_dst) ;
}


