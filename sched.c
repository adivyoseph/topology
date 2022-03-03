#define _GNU_SOURCE
#include <assert.h>
#include <sched.h> /* getcpu */
#include <stdio.h> 
#include <stdlib.h> 
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>   
#include <pthread.h> 
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <dirent.h>


#include "topology.h"
//see header file above for comments

typedef struct llc_hdr_s {
    struct llc_hdr_s *p_next;
    int i_gIndex;
    int i_first;
    int i_last;
    int i_cnt;
    int i_cores[1];
} llc_hdr_t;


// private globals
static    int i_llcCount   ;
static    int i_coreCount  ;
static    int i_coresPerLlc;
static    int i_reserved;

//pool
llc_hdr_t *p_pool_head;
llc_hdr_t *p_pool_last;

//Static
llc_hdr_t *p_static_head;
llc_hdr_t *p_static_last;
int i_staticCores;
int i_staticCoresFree;
int i_staticCoreMax;
llc_hdr_t *p_staticCors[256];

//Reserved

llc_hdr_t *p_reserved_head;
llc_hdr_t *p_reserved_last;

static int buildStaticList(int i_static);
static void displayStaticList(void);
static llc_hdr_t *llc_entryAlloc(void);
static int buildPoolInit(void);
static void displayPool(void);

static int buildReservedInit(int i_ask);
static void displayReserved(void);

/**
 * @brief 
 * 
 * @author root (2 Mar 2022)
 * 
 * @param i_reserved absolute core count
 * @param i_static   absolute core count
 * 
 * @return int 
 */
int schedInit(int i_reserve, int i_static){
    int rc = 0;
    int i_remainder;

    i_llcCount = C_topology.getLlcCount();
    i_coreCount = C_topology.getCoreCount();
    i_coresPerLlc = (int)i_coreCount/i_llcCount;
    i_reserved =  i_reserve;

    printf("cores per llc %d\n", i_coresPerLlc);

    buildPoolInit();
    displayPool();

    i_remainder = buildStaticList(i_static);
    displayPool();
    buildReservedInit(i_reserve);
    displayReserved();

    displayStaticList();
    displayPool();
    return rc;
}



static llc_hdr_t *llc_entryAlloc(void){
    //printf("size %ld\n", sizeof(llc_hdr_t) + sizeof(int)*i_coresPerLlc);
    llc_hdr_t *p_llc = malloc(sizeof(llc_hdr_t) + sizeof(int)*i_coresPerLlc);
    if (p_llc) {
        p_llc->i_first = 0;
        p_llc->i_last  = 0;
        p_llc->i_cnt   = 0;
    }
    return p_llc;
}

static void displayPool(void){
    llc_hdr_t *p_llc_hdr = p_pool_head;
    int i, max;

    while (p_llc_hdr) {
        printf("Pool llc %d  start %d count %d\n",
               p_llc_hdr->i_gIndex,
               p_llc_hdr->i_first,
               p_llc_hdr->i_cnt);
        i = p_llc_hdr->i_first;
        max = i + p_llc_hdr->i_cnt;
        for (; i < max; i++) {
            printf("\tcore%d\n", p_llc_hdr->i_cores[i]);
        }
        p_llc_hdr = p_llc_hdr->p_next;
    }

}


static int buildPoolInit(){
    //i_llcCount
    int i_llc = 0; 
    llc_hdr_t *p_llc_hdr;
    int i;
    int i_cores;
    llc_info_t s_info;

    while (i_llc < i_llcCount) {
        p_llc_hdr = llc_entryAlloc();
        if (p_pool_head == NULL) {
            p_pool_head = p_llc_hdr;
        }
        else {
            p_pool_last->p_next = p_llc_hdr;
        }
        p_pool_last = p_llc_hdr;
        p_llc_hdr->p_next = NULL;

        p_llc_hdr->i_gIndex = i_llc;
        i_cores = C_topology.getLlcInfo(i_llc,  &s_info);
        p_llc_hdr->i_cnt = i_cores;
        p_llc_hdr->i_first = 0;
        p_llc_hdr->i_last  = i_cores -1;
        for (i = 0 ; i < i_cores; i++) {
            p_llc_hdr->i_cores[i] = s_info.i_cores[i];
        }
        i_llc ++;
    }
    return 0;
}



void displayStaticList(void){
    llc_hdr_t *p_llc_hdr = p_static_head;
    int i, max;

    while (p_llc_hdr) {
        printf("Static llc %d  start %d count %d\n",
               p_llc_hdr->i_gIndex,
               p_llc_hdr->i_first,
               p_llc_hdr->i_cnt);
        i = p_llc_hdr->i_first;
        max = i + p_llc_hdr->i_cnt;
        for (; i < max; i++) {
            printf("\tcore%d\n", p_llc_hdr->i_cores[i]);
        }
        p_llc_hdr = p_llc_hdr->p_next;
    }
}

static int buildStaticList(int i_ask){
    int rc = 0;
    int i_remainder = i_ask;
    int i_llc = i_llcCount -1;
    int i;
    llc_hdr_t *p_llc_hdr, *p_work;;

    i_staticCores = i_ask;
    i_staticCoreMax = i_coresPerLlc;


    if (i_coreCount >= (i_ask + i_reserved)) {
        printf("valid ask %d\n", i_ask);
        while (i_remainder && i_llc >= 0) {

            if (i_remainder >= i_coresPerLlc) {
                p_llc_hdr = p_pool_last;
                p_work = p_pool_head;
                while (p_work->p_next != p_llc_hdr) {
                    p_work = p_work->p_next;
                }
                p_pool_last = p_work;
                p_work->p_next = NULL;
                i_remainder -= i_coresPerLlc;
            }
            else {
                p_llc_hdr = llc_entryAlloc();
                p_work = p_pool_last;
                p_llc_hdr->i_gIndex = i_llc;
                i = i_coresPerLlc - i_remainder;
                p_llc_hdr->i_first = i;
                p_llc_hdr->i_last  = i_coresPerLlc;
                p_work->i_last = i -1;
                for (; i < i_coresPerLlc; i++) {
                    p_llc_hdr->i_cores[i] = p_work->i_cores[i];
                }
                p_llc_hdr->i_cnt = i_remainder;
                p_work->i_cnt -= i_remainder;
                i_remainder = 0;
            }

            if (p_static_head == NULL) {
                p_static_head = p_llc_hdr;
            }
            else {
                p_static_last->p_next = p_llc_hdr;
            }
            p_static_last = p_llc_hdr;
            p_llc_hdr->p_next = NULL;

            i_llc --;
        }
    }
    return rc;
}




static int buildReservedInit(int i_ask){

    int rc = 0;
    int i_remainder = i_ask;
    int i_llc;
    int i;
    llc_hdr_t *p_llc_hdr, *p_work;


    if (i_coreCount >= (i_ask + i_reserved)) {
        printf("valid reserved ask %d\n", i_ask);
        i_llc  = p_pool_last->i_gIndex;
        while (i_remainder && i_llc >= 0) {
            if (i_remainder >= p_pool_last->i_cnt) {
                //consume it
                p_llc_hdr = p_pool_last;
                p_work = p_pool_head;
                while (p_work->p_next != p_llc_hdr) {
                    p_work = p_work->p_next;
                }
                p_pool_last = p_work;
                p_work->p_next = NULL;

                i_remainder -= p_llc_hdr->i_cnt;
            }
            else {
                p_llc_hdr = llc_entryAlloc();

                p_llc_hdr->i_last  = p_pool_last->i_last;
                p_llc_hdr->i_gIndex = p_pool_last->i_gIndex;
                p_llc_hdr->i_cnt = i_remainder;
                i = p_pool_last->i_last- i_remainder +1;
                p_llc_hdr->i_first = i;
                for (; i <= p_pool_last->i_last; i++) {
                    p_llc_hdr->i_cores[i] = p_pool_last->i_cores[i];
                }
                p_pool_last->i_cnt -= i_remainder;
                i_remainder = 0;
            }

            if (p_reserved_head == NULL) {
                p_reserved_head = p_llc_hdr;
            }
            else {
                p_reserved_last->p_next = p_llc_hdr;
            }
            p_reserved_last = p_llc_hdr;
            p_llc_hdr->p_next = NULL;

            i_llc --;
        }
    }
    return rc;
}








static void displayReserved(void){
    llc_hdr_t *p_llc_hdr = p_reserved_head;
    int i, max;

    while (p_llc_hdr) {
        printf("Reserved llc %d  start %d count %d\n",
               p_llc_hdr->i_gIndex,
               p_llc_hdr->i_first,
               p_llc_hdr->i_cnt);
        i = p_llc_hdr->i_first;
        max = i + p_llc_hdr->i_cnt;
        for (; i < max; i++) {
            printf("\tcore%d\n", p_llc_hdr->i_cores[i]);
        }
        p_llc_hdr = p_llc_hdr->p_next;
    }
}
