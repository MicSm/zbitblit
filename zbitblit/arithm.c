#include "mio.h"
/********** Adaptive Arithmetic Compression **********/

#define M 15

/* Q1 (= 2 to the M) must be sufficiently large, but not so
   large as the unsigned long 4 * Q1 * (Q1 - 1) overflows.
*/

#define Q1  (1UL << M)
#define Q2  (2 * Q1)
#define Q3  (3 * Q1)
#define Q4  (4 * Q1)
#define MAX_CUM (Q1 - 1)


/* our alphabet */
/* character code = 0, 1, ..., N_CHAR - 1 */
#define MAX_CHAR 258

static uint32 low = 0, high = Q4, value = 0;
static int16  shifts = 0; /* counts for magnifying low and high around Q2 */

typedef struct symb {
          int16  c2s[MAX_CHAR], s2c[MAX_CHAR + 1];
          uint16 sf[MAX_CHAR + 1], scf[MAX_CHAR + 1];
} SYMB;


/* Initialize model */
void StartModel(SYMB *ptr, int16 n_char) {
int16 ch, sym;
 ptr->scf[n_char] = 0;
 /* define start distribution */
 for (sym = n_char; sym >= 1; sym--) {
     ch = sym - 1; ptr->c2s[ch] = sym; ptr->s2c[sym] = ch;
     ptr->sf[sym] = 1; /* here empirical frequency */
     ptr->scf[sym-1] = ptr->scf[sym] + ptr->sf[sym];
 }
 /* sentinel (!= sf[1]) */
 ptr->sf[0] = 0;
}

/* update adaptive model */
void UpdateModel(int sym, SYMB *ptr, int16 n_char) {
int16 i, c, ch_i, ch_sym;

 if (ptr->scf[0] >= MAX_CUM) { /* if overflow */
    c = 0;
    /* scale down frequencies */
    for (i = n_char; i > 0; i--) {
        ptr->scf[i] = c; c += (ptr->sf[i] = (ptr->sf[i] + 1) >> 1);
    }
    ptr->scf[0] = c;
 }
 for (i = sym; ptr->sf[i] == ptr->sf[i - 1]; i--) ;
 if (i < sym) {
    ch_i = ptr->s2c[i]; ch_sym = ptr->s2c[sym]; ptr->s2c[i] = ch_sym;
    ptr->s2c[sym] = ch_i; ptr->c2s[ch_i] = sym; ptr->c2s[ch_sym] = i;
 }
 ptr->sf[i]++; while (--i >= 0) ptr->scf[i]++;
}

/* Output 1 bit, followed by its complements */
void Output(uint8 bit, bfile *fil) {
 bfwrite(bit!=0,fil);
 for (;shifts>0;shifts--) bfwrite(bit!=1,fil);
}

/* encode current char */
void EncodeChar(int16 ch, bfile *fil, SYMB *ptr, int16 n_char) {
int16 sym;
uint32 range;

 sym = ptr->c2s[ch];
 range = high - low;
 high = low + (range * ptr->scf[sym - 1]) / ptr->scf[0];
 low += (range * ptr->scf[sym]) / ptr->scf[0];
 for (;;) {
     if (high <= Q2) Output(0,fil);
     else if (low >= Q2) { Output(1,fil); low -= Q2; high -= Q2; }
          else if (low >= Q1 && high <= Q3) {
                  shifts++; low -= Q1; high -= Q1;
               } else break;
     low<<=1; high<<=1;
 }
 UpdateModel(sym,ptr,n_char);
}

/* must be performed when end of stream */
void EncodeEnd(bfile *fil) {
 shifts++;
 Output(low<Q1?0:1,fil);
}

int16 BinarySearchSym(register uint16 x, SYMB *ptr, int16 n_char) {
int16 i, j, k;

 i = 1; j = n_char;
 while (i<j) {
       k=(i+j)>>1; if (ptr->scf[k]>x) i=k+1; else j=k;
 }
 return i;
}

void StartDecode(bfile *fil) {
int16 i;

 for (i=0; i < M + 2; i++) value=2*value+bfread(fil);
}

int16 DecodeChar(bfile *fil, SYMB *ptr, int16 n_char) {
int16 sym, ch;
uint32 range;

 range = high - low;
 sym = BinarySearchSym((unsigned int)
       (((value-low+1)*ptr->scf[0]-1)/range),ptr,n_char);

 high = low + (range * ptr->scf[sym - 1]) / ptr->scf[0];
 low += (range * ptr->scf[sym]) / ptr->scf[0];

 for (;;) {
     if (low >= Q2) {
        value -= Q2; low -= Q2; high -= Q2;
     } else if (low >= Q1 && high <= Q3) {
               value -= Q1; low -= Q1; high -= Q1;
          } else if (high > Q2) break;
     low<<=1; high<<=1;
     value=2*value+bfread(fil);
 }
 ch=ptr->s2c[sym]; UpdateModel(sym,ptr,n_char);
 return ch;
}
