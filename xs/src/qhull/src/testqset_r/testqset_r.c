/*<html><pre>  -<a                             href="../libqhull/index.htm#TOC"
  >-------------------------------</a><a name="TOP">-</a>

   testset.c -- test qset.c and its use of mem.c

   The test sets are pointers to int.  Normally a set is a pointer to a type (e.g., facetT, ridgeT, etc.).
   For consistency in notation, an "int" is typedef'd to i2T

Functions and macros from qset.h.  Counts occurrences in this test.  Does not correspond to thoroughness.
    qh_setaddsorted -- 4 tests
    qh_setaddnth -- 1 test
    qh_setappend -- 7 tests
    qh_setappend_set -- 1 test
    qh_setappend2ndlast -- 1 test
    qh_setcheck -- lots of tests
    qh_setcompact -- 7 tests
    qh_setcopy -- 3 tests
    qh_setdel -- 1 tests
    qh_setdellast -- 1 tests
    qh_setdelnth -- 2 tests
    qh_setdelnthsorted -- 2 tests
    qh_setdelsorted -- 1 test
    qh_setduplicate -- not testable here
    qh_setequal -- 4 tests
    qh_setequal_except -- 2 tests
    qh_setequal_skip -- 2 tests
    qh_setfree -- 11+ tests
    qh_setfree2 -- not testable here
    qh_setfreelong -- 2 tests
    qh_setin -- 3 tests
    qh_setindex -- 4 tests
    qh_setlarger -- 1 test
    qh_setlast -- 2 tests
    qh_setnew -- 6 tests
    qh_setnew_delnthsorted
    qh_setprint -- tested elsewhere
    qh_setreplace -- 1 test
    qh_setsize -- 9+ tests
    qh_settemp -- 2 tests
    qh_settempfree -- 1 test
    qh_settempfree_all -- 1 test
    qh_settemppop -- 1 test
    qh_settemppush -- 1 test
    qh_settruncate -- 3 tests
    qh_setunique -- 3 tests
    qh_setzero -- 1 test
    FOREACHint_ -- 2 test
    FOREACHint4_
    FOREACHint_i_ -- 1 test
    FOREACHintreverse_
    FOREACHintreverse12_
    FOREACHsetelement_ -- 1 test
    FOREACHsetelement_i_ -- 1 test
    FOREACHsetelementreverse_ -- 1 test
    FOREACHsetelementreverse12_ -- 1 test
    SETelem_ -- 3 tests
    SETelemaddr_ -- 2 tests
    SETelemt_ -- not tested (generic)
    SETempty_ -- 1 test
    SETfirst_ -- 4 tests
    SETfirstt_ -- 2 tests
    SETindex_ -- 2 tests
    SETref_ -- 2 tests
    SETreturnsize_ -- 2 tests
    SETsecond_ -- 1 test
    SETsecondt_ -- 2 tests
    SETtruncate_ -- 2 tests

    Copyright (c) 2012-2015 C.B. Barber. All rights reserved.
    $Id: //main/2015/qhull/src/testqset_r/testqset_r.c#5 $$Change: 2064 $
    $DateTime: 2016/01/18 12:36:08 $$Author: bbarber $
*/

#include "libqhull_r/user_r.h"  /* QHULL_CRTDBG */
#include "libqhull_r/qset_r.h"
#include "libqhull_r/mem_r.h"
#include "libqhull_r/libqhull_r.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int i2T;
#define MAXerrorCount 100 /* quit after n errors */

#define FOREACHint_( ints ) FOREACHsetelement_( i2T, ints, i2)
#define FOREACHint4_( ints ) FOREACHsetelement_( i2T, ints, i4)
#define FOREACHint_i_( qh, ints ) FOREACHsetelement_i_( qh, i2T, ints, i2)
#define FOREACHintreverse_( qh, ints ) FOREACHsetelementreverse_( qh, i2T, ints, i2)
#define FOREACHintreverse12_( ints ) FOREACHsetelementreverse12_( i2T, ints, i2)

enum {
    MAXint= 0x7fffffff,
};

char prompt[]= "testqset_r N [M] [T5] -- Test reentrant qset_r.c and mem_r.c\n\
  \n\
  If this test fails then reentrant Qhull will not work.\n\
  \n\
  Test qsets of 0..N integers with a check every M iterations (default ~log10)\n\
  Additional checking and logging if M is 1\n\
  \n\
  T5 turns on memory logging (qset does not log)\n\
  \n\
  For example:\n\
    testqset_r 10000\n\
";

int error_count= 0;  /* Global error_count.  checkSetContents(qh) keeps its own error count.  It exits on too many errors */

/* Macros normally defined in geom.h */
#define fmax_( a,b )  ( ( a ) < ( b ) ? ( b ) : ( a ) )

/* Macros normally defined in QhullSet.h */

/* Functions normally defined in user_r.h for usermem_r.c */

void    qh_exit(int exitcode);
void    qh_fprintf_stderr(int msgcode, const char *fmt, ... );
void    qh_free(void *mem);
void   *qh_malloc(size_t size);

/* Normally defined in user_r.c */

void    qh_errexit(qhT *qh, int exitcode, facetT *f, ridgeT *r)
{
    (void)f; /* unused */
    (void)r; /* unused */
    (void)qh; /* unused */
    qh_exit(exitcode);
}

/* Normally defined in userprintf.c */

void    qh_fprintf(qhT *qh, FILE *fp, int msgcode, const char *fmt, ... )
{
    static int needs_cr= 0;  /* True if qh_fprintf needs a CR. testqset_r is not itself reentrant */

    size_t fmtlen= strlen(fmt);
    va_list args;

    if (!fp) {
        /* Do not use qh_fprintf_stderr.  This is a standalone program */
        if(!qh)
            fprintf(stderr, "QH6241 qh_fprintf: fp and qh not defined for '%s'", fmt);
        else
            fprintf(stderr, "QH6232 qh_fprintf: fp is 0.  Was wrong qh_fprintf called for '%s'", fmt);
        qh_errexit(qh, 6232, NULL, NULL);
    }
    if(fmtlen>0){
        if(fmt[fmtlen-1]=='\n'){
            if(needs_cr && fmtlen>1){
                fprintf(fp, "\n");
            }
            needs_cr= 0;
        }else{
            needs_cr= 1;
        }
    }
    if(msgcode>=6000 && msgcode<7000){
        fprintf(fp, "Error TQ%d ", msgcode);
    }
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
}

/* Defined below in order of use */
int main(int argc, char **argv);
void readOptions(qhT *qh, int argc, char **argv, const char *promptstr, int *numInts, int *checkEvery, int *traceLevel);
void setupMemory(qhT *qh, int tracelevel, int numInts, int **intarray);

void testSetappendSettruncate(qhT *qh, int numInts, int *intarray, int checkEvery);
void testSetdelSetadd(qhT *qh, int numInts, int *intarray, int checkEvery);
void testSetappendSet(qhT *qh, int numInts, int *intarray, int checkEvery);
void testSetcompactCopy(qhT *qh, int numInts, int *intarray, int checkEvery);
void testSetequalInEtc(qhT *qh, int numInts, int *intarray, int checkEvery);
void testSettemp(qhT *qh, int numInts, int *intarray, int checkEvery);
void testSetlastEtc(qhT *qh, int numInts, int *intarray, int checkEvery);
void testSetdelsortedEtc(qhT *qh, int numInts, int *intarray, int checkEvery);

int log_i(qhT *qh, setT *set, const char *s, int i, int numInts, int checkEvery);
void checkSetContents(qhT *qh, const char *name, setT *set, int count, int rangeA, int rangeB, int rangeC);

int main(int argc, char **argv) {
    int *intarray= NULL;
    int numInts;
    int checkEvery= MAXint;
    int curlong, totlong;
    int traceLevel= 4; /* 4 normally, no tracing since qset does not log.  Option 'T5' for memory tracing */
    qhT qh_qh;
    qhT *qh= &qh_qh;

#if defined(_MSC_VER) && defined(_DEBUG) && defined(QHULL_CRTDBG)  /* user_r.h */
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_DELAY_FREE_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) );
    _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
    _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR );
#endif

    readOptions(qh, argc, argv, prompt, &numInts, &checkEvery, &traceLevel);
    setupMemory(qh, traceLevel, numInts, &intarray);

    testSetappendSettruncate(qh, numInts, intarray, checkEvery);
    testSetdelSetadd(qh, numInts, intarray, checkEvery);
    testSetappendSet(qh, numInts, intarray, checkEvery);
    testSetcompactCopy(qh, numInts, intarray, checkEvery);
    testSetequalInEtc(qh, numInts, intarray, checkEvery);
    testSettemp(qh, numInts, intarray, checkEvery);
    testSetlastEtc(qh, numInts, intarray, checkEvery);
    testSetdelsortedEtc(qh, numInts, intarray, checkEvery);
    printf("\n\nNot testing qh_setduplicate and qh_setfree2.\n  These routines use heap-allocated set contents.  See qhull tests.\n");

    qh_memstatistics(qh, stdout);
    qh_memfreeshort(qh, &curlong, &totlong);
    if (curlong || totlong){
        qh_fprintf(qh, stderr, 8043, "qh_memfreeshort: did not free %d bytes of long memory(%d pieces)\n", totlong, curlong);
        error_count++;
    }
    if(error_count){
        qh_fprintf(qh, stderr, 8012, "testqset: %d errors\n\n", error_count);
        exit(1);
    }else{
        printf("testqset_r: OK\n\n");
    }
    return 0;
}/*main*/

void readOptions(qhT *qh, int argc, char **argv, const char *promptstr, int *numInts, int *checkEvery, int *traceLevel)
{
    long numIntsArg;
    long checkEveryArg;
    char *endp;
    int isTracing= 0;

    if (argc < 2 || argc > 4) {
        printf("%s", promptstr);
        exit(0);
    }
    numIntsArg= strtol(argv[1], &endp, 10);
    if(numIntsArg<1){
        qh_fprintf(qh, stderr, 6301, "First argument should be 1 or greater.  Got '%s'\n", argv[1]);
        exit(1);
    }
    if(numIntsArg>MAXint){
        qh_fprintf(qh, stderr, 6302, "qset does not currently support 64-bit ints.  Maximum count is %d\n", MAXint);
        exit(1);
    }
    *numInts= (int)numIntsArg;

    if(argc==3 && argv[2][0]=='T' && argv[2][1]=='5' ){
        isTracing= 1;
        *traceLevel= 5;
    }
    if(argc==4 || (argc==3 && !isTracing)){
        checkEveryArg= strtol(argv[2], &endp, 10);
        if(checkEveryArg<1){
            qh_fprintf(qh, stderr, 6321, "checkEvery argument should be 1 or greater.  Got '%s'\n", argv[2]);
            exit(1);
        }
        if(checkEveryArg>MAXint){
            qh_fprintf(qh, stderr, 6322, "qset does not currently support 64-bit ints.  Maximum checkEvery is %d\n", MAXint);
            exit(1);
        }
        if(argc==4){
            if(argv[3][0]=='T' && argv[3][1]=='5' ){
                isTracing= 1;
                *traceLevel= 5;
            }else{
                qh_fprintf(qh, stderr, 6242, "Optional third argument must be 'T5'.  Got '%s'\n", argv[3]);
                exit(1);
            }
        }
        *checkEvery= (int)checkEveryArg;
    }
}/*readOptions*/

void setupMemory(qhT *qh, int tracelevel, int numInts, int **intarray)
{
    int i;
    if(numInts<0 || numInts*(int)sizeof(int)<0){
        qh_fprintf(qh, stderr, 6303, "qset does not currently support 64-bit ints.  Integer overflow\n");
        exit(1);
    }
    *intarray= qh_malloc(numInts * sizeof(int));
    if(!*intarray){
        qh_fprintf(qh, stderr, 6304, "Failed to allocate %d bytes of memory\n", numInts * sizeof(int));
        exit(1);
    }
    for(i= 0; i<numInts; i++){
        (*intarray)[i] =i;
    }

    qh_meminit(qh, stderr);
    qh_meminitbuffers(qh, tracelevel, qh_MEMalign, 4 /*sizes*/, qh_MEMbufsize, qh_MEMinitbuf);
    qh_memsize(qh, 10);
    qh_memsize(qh, 20);
    qh_memsize(qh, 30);
    qh_memsize(qh, 40);
    qh_memsetup(qh);

    qh_fprintf(qh, stderr, 8001, "SETelemsize is %d bytes for pointer-to-int\n", SETelemsize);
}/*setupMemmory*/

void testSetappendSettruncate(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints= qh_setnew(qh, 4);
    int i, isCheck;

    qh_fprintf(qh, stderr, 8002, "\n\nTesting qh_setappend 0..%d.  Test", numInts-1);
    for(i= 0; i<numInts; i++){
        isCheck= log_i(qh, ints, "i", i, numInts, checkEvery);
        qh_setappend(qh, &ints, intarray+i);
        if(isCheck){
            checkSetContents(qh, "qh_setappend", ints, i+1, 0, -1, -1);
        }
    }

    qh_fprintf(qh, stderr, 8014, "\n\nTesting qh_settruncate %d and 0.  Test", numInts/2);
    if(numInts>=2){
        isCheck= log_i(qh, ints, "n", numInts/2, numInts, checkEvery);
        qh_settruncate(qh, ints, numInts/2);
        checkSetContents(qh, "qh_settruncate by half", ints, numInts/2, 0, -1, -1);
    }
    isCheck= log_i(qh, ints, "n", 0, numInts, checkEvery);
    qh_settruncate(qh, ints, 0);
    checkSetContents(qh, "qh_settruncate", ints, 0, -1, -1, -1);

    qh_fprintf(qh, stderr, 8003, "\n\nTesting qh_setappend2ndlast 0,0..%d.  Test 0", numInts-1);
    qh_setfree(qh, &ints);
    ints= qh_setnew(qh, 4);
    qh_setappend(qh, &ints, intarray+0);
    for(i= 0; i<numInts; i++){
        isCheck= log_i(qh, ints, "i", i, numInts, checkEvery);
        qh_setappend2ndlast(qh, &ints, intarray+i);
        if(isCheck){
            checkSetContents(qh, "qh_setappend2ndlast", ints, i+2, 0, 0, -1);
        }
    }
    qh_fprintf(qh, stderr, 8015, "\n\nTesting SETtruncate_ %d and 0.  Test", numInts/2);
    if(numInts>=2){
        isCheck= log_i(qh, ints, "n", numInts/2, numInts, checkEvery);
        SETtruncate_(ints, numInts/2);
        checkSetContents(qh, "SETtruncate_ by half", ints, numInts/2, 0, -1, -1);
    }
    isCheck= log_i(qh, ints, "n", 0, numInts, checkEvery);
    SETtruncate_(ints, 0);
    checkSetContents(qh, "SETtruncate_", ints, 0, -1, -1, -1);

    qh_setfree(qh, &ints);
}/*testSetappendSettruncate*/

void testSetdelSetadd(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints=qh_setnew(qh, 1);
    int i,j,isCheck;

    qh_fprintf(qh, stderr, 8003, "\n\nTesting qh_setdelnthsorted and qh_setaddnth 1..%d. Test", numInts-1);
    for(j=1; j<numInts; j++){  /* size 0 not valid */
        if(log_i(qh, ints, "j", j, numInts, MAXint)){
            for(i= qh_setsize(qh, ints); i<j; i++){
                qh_setappend(qh, &ints, intarray+i);
            }
            checkSetContents(qh, "qh_setappend", ints, j, 0, -1, -1);
            for(i= 0; i<j && i<100; i++){  /* otherwise too slow */
                isCheck= log_i(qh, ints, "", i, numInts, checkEvery);
                (void)isCheck; /* unused */
                qh_setdelnthsorted(qh, ints, i);
                qh_setaddnth(qh, &ints, i, intarray+i);
                if(checkEvery==1){
                    checkSetContents(qh, "qh_setdelnthsorted qh_setaddnth", ints, j, 0, -1, -1);
                }
            }
            checkSetContents(qh, "qh_setdelnthsorted qh_setaddnth 2", ints, j, 0, -1, -1);
        }
    }
    qh_setfree(qh, &ints);
}/*testSetdelSetadd*/

void testSetappendSet(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints=qh_setnew(qh, 1);
    setT *ints2;
    int i,j,k;

    qh_fprintf(qh, stderr, 8016, "\n\nTesting qh_setappend_set 0..%d. Test", numInts-1);
    for(j=0; j<numInts; j++){
        if(log_i(qh, ints, "j", j, numInts, numInts)){
            for(i= qh_setsize(qh, ints); i<j; i++){
                qh_setappend(qh, &ints, intarray+i);
            }
            if(checkEvery==1){
                checkSetContents(qh, "qh_setappend", ints, j, 0, -1, -1);
            }
            ints2= qh_setnew(qh, j==0 ? 0 : j-1);  /* One less than needed */
            for(i= 0; i<=j && i<=20; i++){  /* otherwise too slow */
                if(log_i(qh, ints, "", i, numInts, numInts)){
                    for(k= qh_setsize(qh, ints2); k<i; k++){
                        qh_setappend(qh, &ints2, intarray+k);
                    }
                    if(checkEvery==1){
                        checkSetContents(qh, "qh_setappend 2", ints2, i, 0, -1, -1);
                    }
                    qh_setappend_set(qh, &ints, ints2);
                    checkSetContents(qh, "qh_setappend_set", ints, i+j, 0, (j==0 ? -1 : 0), -1);
                    qh_settruncate(qh, ints, j);
                    if(checkEvery==1){
                        checkSetContents(qh, "qh_settruncate", ints, j, 0, -1, -1);
                    }
                }
            }
            qh_setfree(qh, &ints2);
        }
    }
    qh_setfree(qh, &ints);
}/*testSetappendSet*/

void testSetcompactCopy(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints= qh_setnew(qh, 20);
    setT *ints2= NULL;
    int i,j,k;

    qh_fprintf(qh, stderr, 8017, "\n\nTesting qh_setcompact and qh_setcopy 0..%d. Test", numInts-1);
    for(j=0; j<numInts; j++){
        if(log_i(qh, ints, "j", j, numInts, checkEvery)){
            for(i= qh_setsize(qh, ints); i<j; i++){  /* Test i<j to test the empty set */
                for(k= 0; k<i%7; k++){
                    qh_setappend(qh, &ints, NULL);
                }
                qh_setappend(qh, &ints, intarray+i);
            }
            qh_setfree(qh, &ints2);
            ints2= qh_setcopy(qh, ints, 0);
            qh_setcompact(qh, ints);
            qh_setcompact(qh, ints2);
            checkSetContents(qh, "qh_setcompact", ints, j, 0, 0, -1);
            checkSetContents(qh, "qh_setcompact", ints2, j, 0, 0, -1);
            qh_setcompact(qh, ints);
            checkSetContents(qh, "qh_setcompact", ints, j, 0, 0, -1);
        }
    }
    qh_setfree(qh, &ints);
    qh_setfree(qh, &ints2);
}/*testSetcompactCopy*/

void testSetdelsortedEtc(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints= qh_setnew(qh, 1);
    setT *ints2= NULL;
    int i,j;

    qh_fprintf(qh, stderr, 8018, "\n\nTesting qh_setdel*, qh_setaddsorted, and  0..%d. Test", numInts-1);
    for(j=0; j<numInts; j++){
        if(log_i(qh, ints, "j", j, numInts, checkEvery)){
            for(i= qh_setsize(qh, ints); i<j; i++){  /* Test i<j to test the empty set */
                qh_setaddsorted(qh, &ints, intarray+i);
            }
            checkSetContents(qh, "qh_setaddsorted", ints, j, 0, 0, -1);
            if(j>3){
                qh_setdelsorted(ints, intarray+i/2);
                checkSetContents(qh, "qh_setdelsorted", ints, j-1, 0, i/2+1, -1);
                qh_setaddsorted(qh, &ints, intarray+i/2);
                checkSetContents(qh, "qh_setaddsorted i/2", ints, j, 0, 0, -1);
            }
            qh_setdellast(ints);
            checkSetContents(qh, "qh_setdellast", ints, (j ? j-1 : 0), 0, -1, -1);
            if(j>0){
                qh_setaddsorted(qh, &ints, intarray+j-1);
                checkSetContents(qh, "qh_setaddsorted j-1", ints, j, 0, -1, -1);
            }
            if(j>4){
                qh_setdelnthsorted(qh, ints, i/2);
                if (checkEvery==1)
                  checkSetContents(qh, "qh_setdelnthsorted", ints, j-1, 0, i/2+1, -1);
                /* test qh_setdelnth and move-to-front */
                qh_setdelsorted(ints, intarray+i/2+1);
                checkSetContents(qh, "qh_setdelsorted 2", ints, j-2, 0, i/2+2, -1);
                qh_setaddsorted(qh, &ints, intarray+i/2+1);
                if (checkEvery==1)
                  checkSetContents(qh, "qh_setaddsorted i/2+1", ints, j-1, 0, i/2+1, -1);
                qh_setaddsorted(qh, &ints, intarray+i/2);
                checkSetContents(qh, "qh_setaddsorted i/2 again", ints, j, 0, -1, -1);
            }
            qh_setfree(qh, &ints2);
            ints2= qh_setcopy(qh, ints, 0);
            qh_setcompact(qh, ints);
            qh_setcompact(qh, ints2);
            checkSetContents(qh, "qh_setcompact", ints, j, 0, 0, -1);
            checkSetContents(qh, "qh_setcompact 2", ints2, j, 0, 0, -1);
            qh_setcompact(qh, ints);
            checkSetContents(qh, "qh_setcompact 3", ints, j, 0, 0, -1);
            qh_setfree(qh, &ints2);
        }
    }
    qh_setfreelong(qh, &ints);
    if(ints){
        qh_setfree(qh, &ints); /* Was quick memory */
    }
}/*testSetdelsortedEtc*/

void testSetequalInEtc(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints= NULL;
    setT *ints2= NULL;
    setT *ints3= NULL;
    int i,j,n;

    qh_fprintf(qh, stderr, 8019, "\n\nTesting qh_setequal*, qh_setin*, qh_setdel, qh_setdelnth, and qh_setlarger 0..%d. Test", numInts-1);
    for(j=0; j<numInts; j++){
        if(log_i(qh, ints, "j", j, numInts, checkEvery)){
            n= qh_setsize(qh, ints);
            qh_setlarger(qh, &ints);
            checkSetContents(qh, "qh_setlarger", ints, n, 0, -1, -1);
            for(i= qh_setsize(qh, ints); i<j; i++){  /* Test i<j to test the empty set */
                qh_setappend(qh, &ints, intarray+i);
            }
            checkSetContents(qh, "qh_setappend", ints, j, 0, -1, -1);
            if(!qh_setequal(ints, ints)){
                qh_fprintf(qh, stderr, 6300, "testSetequalInEtc: set not equal to itself at length %d\n", j);
                error_count++;
            }
            if(j==0 && !qh_setequal(ints, ints2)){
                qh_fprintf(qh, stderr, 6323, "testSetequalInEtc: empty set not equal to null set\n");
                error_count++;
            }
            if(j>0){
                if(qh_setequal(ints, ints2)){
                    qh_fprintf(qh, stderr, 6324, "testSetequalInEtc: non-empty set equal to empty set\n", j);
                    error_count++;
                }
                qh_setfree(qh, &ints3);
                ints3= qh_setcopy(qh, ints, 0);
                checkSetContents(qh, "qh_setreplace", ints3, j, 0, -1, -1);
                qh_setreplace(qh, ints3, intarray+j/2, intarray+j/2+1);
                if(j==1){
                    checkSetContents(qh, "qh_setreplace 2", ints3, j, j/2+1, -1, -1);
                }else if(j==2){
                    checkSetContents(qh, "qh_setreplace 3", ints3, j, 0, j/2+1, -1);
                }else{
                    checkSetContents(qh, "qh_setreplace 3", ints3, j, 0, j/2+1, j/2+1);
                }
                if(qh_setequal(ints, ints3)){
                    qh_fprintf(qh, stderr, 6325, "testSetequalInEtc: modified set equal to original set at %d/2\n", j);
                    error_count++;
                }
                if(!qh_setequal_except(ints, intarray+j/2, ints3, intarray+j/2+1)){
                    qh_fprintf(qh, stderr, 6326, "qh_setequal_except: modified set not equal to original set except modified\n", j);
                    error_count++;
                }
                if(qh_setequal_except(ints, intarray+j/2, ints3, intarray)){
                    qh_fprintf(qh, stderr, 6327, "qh_setequal_except: modified set equal to original set with wrong excepts\n", j);
                    error_count++;
                }
                if(!qh_setequal_skip(ints, j/2, ints3, j/2)){
                    qh_fprintf(qh, stderr, 6328, "qh_setequal_skip: modified set not equal to original set except modified\n", j);
                    error_count++;
                }
                if(j>2 && qh_setequal_skip(ints, j/2, ints3, 0)){
                    qh_fprintf(qh, stderr, 6329, "qh_setequal_skip: modified set equal to original set with wrong excepts\n", j);
                    error_count++;
                }
                if(intarray+j/2+1!=qh_setdel(ints3, intarray+j/2+1)){
                    qh_fprintf(qh, stderr, 6330, "qh_setdel: failed to find added element\n", j);
                    error_count++;
                }
                checkSetContents(qh, "qh_setdel", ints3, j-1, 0, j-1, (j==1 ? -1 : j/2+1));  /* swaps last element with deleted element */
                if(j>3){
                    qh_setdelnth(qh, ints3, j/2); /* Delete at the same location as the original replace, for only one out-of-order element */
                    checkSetContents(qh, "qh_setdelnth", ints3, j-2, 0, j-2, (j==2 ? -1 : j/2+1));
                }
                if(qh_setin(ints3, intarray+j/2)){
                    qh_fprintf(qh, stderr, 6331, "qh_setin: found deleted element\n");
                    error_count++;
                }
                if(j>4 && !qh_setin(ints3, intarray+1)){
                    qh_fprintf(qh, stderr, 6332, "qh_setin: did not find second element\n");
                    error_count++;
                }
                if(j>4 && !qh_setin(ints3, intarray+j-2)){
                    qh_fprintf(qh, stderr, 6333, "qh_setin: did not find last element\n");
                    error_count++;
                }
                if(-1!=qh_setindex(ints2, intarray)){
                    qh_fprintf(qh, stderr, 6334, "qh_setindex: found element in empty set\n");
                    error_count++;
                }
                if(-1!=qh_setindex(ints3, intarray+j/2)){
                    qh_fprintf(qh, stderr, 6335, "qh_setindex: found deleted element in set\n");
                    error_count++;
                }
                if(0!=qh_setindex(ints, intarray)){
                    qh_fprintf(qh, stderr, 6336, "qh_setindex: did not find first in set\n");
                    error_count++;
                }
                if(j-1!=qh_setindex(ints, intarray+j-1)){
                    qh_fprintf(qh, stderr, 6337, "qh_setindex: did not find last in set\n");
                    error_count++;
                }
            }
            qh_setfree(qh, &ints2);
        }
    }
    qh_setfree(qh, &ints3);
    qh_setfreelong(qh, &ints);
    if(ints){
        qh_setfree(qh, &ints); /* Was quick memory */
    }
}/*testSetequalInEtc*/


void testSetlastEtc(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints= NULL;
    setT *ints2= NULL;
    int i,j,prepend;

    qh_fprintf(qh, stderr, 8020, "\n\nTesting qh_setlast, qh_setnew_delnthsorted, qh_setunique, and qh_setzero 0..%d. Test", numInts-1);
    for(j=0; j<numInts; j++){
        if(log_i(qh, ints, "j", j, numInts, checkEvery)){
            for(i= qh_setsize(qh, ints); i<j; i++){  /* Test i<j to test the empty set */
                if(!qh_setunique(qh, &ints, intarray+i)){
                    qh_fprintf(qh, stderr, 6340, "qh_setunique: not able to append next element %d\n", i);
                    error_count++;
                }
                if(checkEvery==1){
                    checkSetContents(qh, "qh_setunique", ints, i+1, 0, -1, -1);
                }
                if(qh_setunique(qh, &ints, intarray+i)){
                    qh_fprintf(qh, stderr, 6341, "qh_setunique: appended next element twice %d\n", i);
                    error_count++;
                }
                if(qh_setunique(qh, &ints, intarray+i/2)){
                    qh_fprintf(qh, stderr, 6346, "qh_setunique: appended middle element twice %d/2\n", i);
                    error_count++;
                }
            }
            checkSetContents(qh, "qh_setunique 2", ints, j, 0, -1, -1);
            if(j==0 && NULL!=qh_setlast(ints)){
                qh_fprintf(qh, stderr, 6339, "qh_setlast: returned last element of empty set\n");
                error_count++;
            }
            if(j>0){
                if(intarray+j-1!=qh_setlast(ints)){
                    qh_fprintf(qh, stderr, 6338, "qh_setlast: wrong last element\n");
                    error_count++;
                }
                prepend= (j<100 ? j/4 : 0);
                ints2= qh_setnew_delnthsorted(qh, ints, qh_setsize(qh, ints), j/2, prepend);
                if(qh_setsize(qh, ints2)!=j+prepend-1){
                    qh_fprintf(qh, stderr, 6345, "qh_setnew_delnthsorted: Expecting %d elements, got %d\n", j+prepend-1, qh_setsize(qh, ints2));
                    error_count++;
                }
                /* Define prepended elements.  Otherwise qh_setdelnthsorted may fail */
                for(i= 0; i<prepend; i++){
                    void **p= &SETelem_(ints2, i);
                    *p= intarray+0;
                }
                for(i= 0; i<prepend; i++){
                    qh_setdelnthsorted(qh, ints2, 0);  /* delete undefined prefix */
                }
                checkSetContents(qh, "qh_setnew_delnthsorted", ints2, j-1, 0, j/2+1, -1);
                if(j>2){
                    qh_setzero(qh, ints2, j/2, j-1);  /* max size may be j-1 */
                    if(qh_setsize(qh, ints2)!=j-1){
                        qh_fprintf(qh, stderr, 6342, "qh_setzero: Expecting %d elements, got %d\n", j, qh_setsize(qh, ints2));
                        error_count++;
                    }
                    qh_setcompact(qh, ints2);
                    checkSetContents(qh, "qh_setzero", ints2, j/2, 0, -1, -1);
                }
            }
            qh_setfree(qh, &ints2);
        }
    }
    qh_setfreelong(qh, &ints);
    if(ints){
        qh_setfree(qh, &ints); /* Was quick memory */
    }
}/*testSetlastEtc*/

void testSettemp(qhT *qh, int numInts, int *intarray, int checkEvery)
{
    setT *ints= NULL;
    setT *ints2= NULL;
    setT *ints3= NULL;
    int i,j;

    qh_fprintf(qh, stderr, 8021, "\n\nTesting qh_settemp* 0..%d. Test", numInts-1);
    for(j=0; j<numInts; j++){
        if(log_i(qh, ints, "j", j, numInts, checkEvery)){
            if(j<20){
                for(i=0; i<j; i++){
                    ints2= qh_settemp(qh, j);
                }
                qh_settempfree_all(qh);
            }
            for(i= qh_setsize(qh, ints); i<j; i++){  /* Test i<j to test the empty set */
                qh_setappend(qh, &ints, intarray+i);
            }
            ints2= qh_settemp(qh, j);
            if(j>0){
                qh_settemppush(qh, ints);
                ints3= qh_settemppop(qh);
                if(ints!=ints3){
                    qh_fprintf(qh, stderr, 6343, "qh_settemppop: didn't pop the push\n");
                    error_count++;
                }
            }
            qh_settempfree(qh, &ints2);
        }
    }
    qh_setfreelong(qh, &ints);
    if(ints){
        qh_setfree(qh, &ints); /* Was quick memory */
    }
}/*testSettemp*/

/* Check that a set contains count elements
   Ranges are consecutive (e.g., 1,2,3,...) starting with first, mid, and last
   Use -1 for missing ranges
   Returns -1 if should check results
*/
int log_i(qhT *qh, setT *set, const char *s, int i, int numInts, int checkEvery)
{
    int j= i;
    int scale= 1;
    int e= 0;
    int *i2, **i2p;

    if(*s || checkEvery==1){
        if(i<10){
            qh_fprintf(qh, stderr, 8004, " %s%d", s, i);
        }else{
            if(i==11 && checkEvery==1){
                qh_fprintf(qh, stderr, 8005, "\nResults after 10: ");
                FOREACHint_(set){
                    qh_fprintf(qh, stderr, 8006, " %d", *i2);
                }
                qh_fprintf(qh, stderr, 8007, " Continue");
            }
            while((j= j/10)>=1){
                scale *= 10;
                e++;
            }
            if(i==numInts-1){
                qh_fprintf(qh, stderr, 8008, " %s%d", s, i);
            }else if(i==scale){
                if(i<=1000){
                    qh_fprintf(qh, stderr, 8010, " %s%d", s, i);
                }else{
                    qh_fprintf(qh, stderr, 8009, " %s1e%d", s, e);
                }
            }
        }
    }
    if(i<1000 || i%checkEvery==0 || i== scale || i==numInts-1){
        return 1;
    }
    return 0;
}/*log_i*/

/* Check that a set contains count elements
   Ranges are consecutive (e.g., 1,2,3,...) starting with first, mid, and last
   Use -1 for missing ranges
*/
void checkSetContents(qhT *qh, const char *name, setT *set, int count, int rangeA, int rangeB, int rangeC)
{

    i2T *i2, **i2p;
    int i2_i, i2_n;
    int prev= -1; /* avoid warning */
    int i;
    int first= -3;
    int second= -3;
    int rangeCount=1;
    int actualSize= 0;

    qh_setcheck(qh, set, name, 0);
    if(set){
        SETreturnsize_(set, actualSize);  /* normally used only when speed is critical */
        if(*qh_setendpointer(set)!=NULL){
            qh_fprintf(qh, stderr, 6344, "%s: qh_setendpointer(set), 0x%x, is not NULL terminator of set 0x%x", name, qh_setendpointer(set), set);
            error_count++;
        }
    }
    if(actualSize!=qh_setsize(qh, set)){
        qh_fprintf(qh, stderr, 6305, "%s: SETreturnsize_(qh) returned %d while qh_setsize(qh) returns %d\n", name, actualSize, qh_setsize(qh, set));
        error_count++;
    }else if(actualSize!=count){
        qh_fprintf(qh, stderr, 6306, "%s: Expecting %d elements for set.  Got %d elements\n", name, count, actualSize);
        error_count++;
    }
    if(SETempty_(set)){
        if(count!=0){
            qh_fprintf(qh, stderr, 6307, "%s: Got empty set instead of count %d, rangeA %d, rangeB %d, rangeC %d\n", name, count, rangeA, rangeB, rangeC);
            error_count++;
        }
    }else{
        /* Must be first, otherwise trips msvc 8 */
        i2T **p= SETaddr_(set, i2T);
        if(*p!=SETfirstt_(set, i2T)){
            qh_fprintf(qh, stderr, 6309, "%s: SETaddr_(set, i2t) [%p] is not the same as SETfirst_(set) [%p]\n", name, SETaddr_(set, i2T), SETfirst_(set));
            error_count++;
        }
        first= *(int *)SETfirst_(set);
        if(SETfirst_(set)!=SETfirstt_(set, i2T)){
            qh_fprintf(qh, stderr, 6308, "%s: SETfirst_(set) [%p] is not the same as SETfirstt_(set, i2T [%p]\n", name, SETfirst_(set), SETfirstt_(set, i2T));
            error_count++;
        }
        if(qh_setsize(qh, set)>1){
            second= *(int *)SETsecond_(set);
            if(SETsecond_(set)!=SETsecondt_(set, i2T)){
                qh_fprintf(qh, stderr, 6310, "%s: SETsecond_(set) [%p] is not the same as SETsecondt_(set, i2T) [%p]\n", name, SETsecond_(set), SETsecondt_(set, i2T));
                error_count++;
            }
        }
    }
    /* Test first run of ints in set*/
    i= 0;
    FOREACHint_(set){
        if(i2!=SETfirst_(set) && *i2!=prev+1){
            break;
        }
        prev= *i2;
        if(SETindex_(set, i2)!=i){
            qh_fprintf(qh, stderr, 6311, "%s: Expecting SETindex_(set, pointer-to-%d) to be %d.  Got %d\n", name, *i2, i, SETindex_(set, i2));
            error_count++;;
        }
        if(i2!=SETref_(i2)){
            qh_fprintf(qh, stderr, 6312, "%s: SETref_(i2) [%p] does not point to i2 (the %d'th element)\n", name, SETref_(i2), i);
            error_count++;;
        }
        i++;
    }
    FOREACHint_i_(qh, set){
        /* Must be first conditional, otherwise it trips up msvc 8 */
        i2T **p= SETelemaddr_(set, i2_i, i2T);
        if(i2!=*p){
            qh_fprintf(qh, stderr, 6320, "%s: SETelemaddr_(set, %d, i2T) [%p] does not point to i2\n", name, i2_i, SETelemaddr_(set, i2_i, int));
            error_count++;;
        }
        if(i2_i==0){
            if(first!=*i2){
                qh_fprintf(qh, stderr, 6314, "%s: First element is %d instead of SETfirst %d\n", name, *i2, first);
                error_count++;;
            }
            if(rangeA!=*i2){
                qh_fprintf(qh, stderr, 6315, "%s: starts with %d instead of rangeA %d\n", name, *i2, rangeA);
                error_count++;;
            }
            prev= rangeA;
        }else{
            if(i2_i==1 && second!=*i2){
                qh_fprintf(qh, stderr, 6316, "%s: Second element is %d instead of SETsecond %d\n", name, *i2, second);
                error_count++;;
            }
            if(prev+1==*i2){
                prev++;
            }else{
                if(*i2==rangeB){
                    prev= rangeB;
                    rangeB= -1;
                    rangeCount++;
                }else if(rangeB==-1 && *i2==rangeC){
                    prev= rangeC;
                    rangeC= -1;
                    rangeCount++;
                }else{
                    prev++;
                    qh_fprintf(qh, stderr, 6317, "%s: Expecting %d'th element to be %d.  Got %d\n", name, i2_i, prev, *i2);
                    error_count++;
                }
            }
        }
        if(i2!=SETelem_(set, i2_i)){
            qh_fprintf(qh, stderr, 6318, "%s: SETelem_(set, %d) [%p] is not i2 [%p] (the %d'th element)\n", name, i2_i, SETelem_(set, i2_i), i2, i2_i);
            error_count++;;
        }
        if(SETelemt_(set, i2_i, i2T)!=SETelem_(set, i2_i)){   /* Normally SETelemt_ is used for generic sets */
            qh_fprintf(qh, stderr, 6319, "%s: SETelemt_(set, %d, i2T) [%p] is not SETelem_(set, %d) [%p] (the %d'th element)\n", name, i2_i, SETelemt_(set, i2_i, int), i2_i, SETelem_(set, i2_i), i2_i);
            error_count++;;
        }
    }
    if(error_count>=MAXerrorCount){
        qh_fprintf(qh, stderr, 8011, "testqset: Stop testing after %d errors\n", error_count);
        exit(1);
    }
}/*checkSetContents*/

