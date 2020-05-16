#ifndef _sort3_h
#define _sort3_h

long stack_start[32], stack_end[32];

#define SWP(xx,yy) { swp=base[xx];base[xx]=base[yy];base[yy]=swp; }

static void qsort4(unsigned long *base, long nelem,
            int (*fcmp)(unsigned long,  unsigned long)) {

register long left, right, num, j, i, middle, stack_on;
register unsigned long swp;

 stack_on = 0 ;
 stack_start[0] = 0 ;
 stack_end[0] = nelem - 1 ;

 while(stack_on>=0) {

      left=stack_start[stack_on] ;
      right=stack_end[stack_on] ;
      stack_on-- ;

      /* Sort items 'left' to 'right' */
      while (left<right) {

            /* Pick the middle item based on a sample of 3 items. */
            num=right-left ;
            if (num<2) {
               if (num==1)  /* Two Items */
                  if (fcmp(base[left],base[right])>0) SWP(left,right)
               break ;
            }

            /* Choose 'ptr_ptr[left]' to be a median of three values */
            middle=(right+left)>>1;

            if (fcmp(base[middle],base[right])>0) SWP(middle,right)

            if (fcmp(base[middle],base[left])>0) SWP(left,middle)
             else
              if (fcmp(base[left],base[right])>0) SWP(left,right)

            if (num==2 ) {  /* Special Optimization on Three Items */
               SWP(left,middle)
               break;
            }

            i=left+1 ;
            while (fcmp(base[left],base[i])>0) i++;

            j=right ;
            while(fcmp(base[j],base[left])>0) j--;

            while (i<j) {
                  SWP(i,j)
                  i++ ;
                  while(fcmp(base[left],base[i])>0) i++;
                  j--;
                  while(fcmp(base[j],base[left])>0) j--;
            }

            SWP(left,j)

            /* Both Sides are non-trivial */
            if (j-left>right-j)  {
               /* Left sort is larger, put it on the stack */
               stack_start[++stack_on]=left;
               stack_end[stack_on]=j-1;
               left=j+1;
            } else {
               /* Right sort is larger, put it on the stack */
               stack_start[++stack_on]=j+1 ;
               stack_end[stack_on]=right;
               right=j-1;
              }
      }
 }
}

static void qsort1(unsigned char *base, long nelem,
            int (*fcmp)(unsigned char,  unsigned char)) {

register long left, right, num, j, i, middle, stack_on;
register unsigned char swp;

 stack_on = 0 ;
 stack_start[0] = 0 ;
 stack_end[0] = nelem - 1 ;

 while(stack_on>=0) {

      left=stack_start[stack_on] ;
      right=stack_end[stack_on] ;
      stack_on-- ;

      /* Sort items 'left' to 'right' */
      while (left<right) {

            /* Pick the middle item based on a sample of 3 items. */
            num=right-left ;
            if (num<2) {
               if (num==1)  /* Two Items */
                  if (fcmp(base[left],base[right])>0) SWP(left,right)
               break ;
            }

            /* Choose 'ptr_ptr[left]' to be a median of three values */
            middle=(right+left)>>1;

            if (fcmp(base[middle],base[right])>0) SWP(middle,right)

            if (fcmp(base[middle],base[left])>0) SWP(left,middle)
             else
              if (fcmp(base[left],base[right])>0) SWP(left,right)

            if (num==2 ) {  /* Special Optimization on Three Items */
               SWP(left,middle)
               break;
            }

            i=left+1 ;
            while (fcmp(base[left],base[i])>0) i++;

            j=right ;
            while(fcmp(base[j],base[left])>0) j--;

            while (i<j) {
                  SWP(i,j)
                  i++ ;
                  while(fcmp(base[left],base[i])>0) i++;
                  j--;
                  while(fcmp(base[j],base[left])>0) j--;
            }

            SWP(left,j)

            /* Both Sides are non-trivial */
            if (j-left>right-j)  {
               /* Left sort is larger, put it on the stack */
               stack_start[++stack_on]=left;
               stack_end[stack_on]=j-1;
               left=j+1;
            } else {
               /* Right sort is larger, put it on the stack */
               stack_start[++stack_on]=j+1 ;
               stack_end[stack_on]=right;
               right=j-1;
              }
      }
 }
}

#endif
