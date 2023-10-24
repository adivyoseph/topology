/*
    Actor simulator
*/



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

#include "topology.h"
#include "workq.h"


//
typedef struct actorThreadContext_s {
    workq_t workq_in;
    int node;
    int llcGroup;
    int core;
    int cpu;
    int osId;


    int srcId;
    int state;
    char name[32];
    pthread_t   thread_id;

    //stats
    unsigned int errors;

} actorThreadContext_t;


// directory services
typedef struct dir_cpu_s {
    int state;
    actorThreadContext_t context;
} dir_cpu_t;
#define DIR_CPUCNT_MAX 32

typedef struct dir_llcGroup_s {
    int coreCnt;
    int cpuCnt;
    dir_cpu_t cpus[DIR_CPUCNT_MAX];
} dir_llcGroup_t;
#define DIR_LLCGROUP_MAX 16

typedef struct dir_node_s {
    int llcGroupCnt;
    dir_llcGroup_t llcGroups[DIR_LLCGROUP_MAX];
} dir_node_t;
dir_node_t __nodes[2];

void dir_init(void){
    int i,j;
    actorThreadContext_t *p_context;

    __nodes[0].llcGroupCnt = topo_getLLCgroupsCnt();
    for (i = 0; i < __nodes[0].llcGroupCnt; i++) {
        __nodes[0].llcGroups[i].coreCnt = topo_getCoresPerLLCgroup();
        __nodes[0].llcGroups[i].cpuCnt = __nodes[0].llcGroups[i].coreCnt * (1 + topo_getSMTOn());
        for (j = 0; j < __nodes[0].llcGroups[i].cpuCnt; j++) {
            __nodes[0].llcGroups[i].cpus[j].state = 0;
            p_context = &__nodes[0].llcGroups[i].cpus[j].context;
            p_context->node = 0;
            p_context->llcGroup = i;
            if (topo_getSMTOn() > 0) {
                p_context->core = j /2;
                p_context->cpu = j & 0x001;
                p_context->osId = topo_getOsId(0, i,p_context->core,p_context->cpu);
            }
            else{
                p_context->core = j;
                p_context->cpu = 0;
                p_context->osId = topo_getOsId(0, i,j,0);
            }

        }
    }
}

actorThreadContext_t *dir_getContext(int node, int llcgroup, int core, int cpu){
    actorThreadContext_t *p_context = NULL;
     int i;
    if (node == 0) {
        if (llcgroup < __nodes[node].llcGroupCnt) {
            if (core >= 0) {

            }
            else {
                if (cpu >= 0) {
                }
                else {
                    // find next avalable cpu
                    for (i = 0; i < __nodes[node].llcGroups[llcgroup].cpuCnt; i++) {
                        if (__nodes[node].llcGroups[llcgroup].cpus[i].state == 0) {
                            __nodes[node].llcGroups[llcgroup].cpus[i].state = 1;
                            p_context = &__nodes[node].llcGroups[llcgroup].cpus[i].context;
                            break;
                        }


                    }
                }
            }
        }
    }
    return p_context;
}


void *th_send(void *p_arg);
void *th_recv(void *p_arg);

// commands
#define CMD_CTL_INIT        1
#define CMD_CTL_READY       2
#define CMD_CTL_START       3
#define CMD_CTL_STOP        4
#define CMD_CTL_CLEAR       5
#define CMD_REQ_LAST        8
#define CMD_REQ             9
#define CMD_REQ_ACK        10

// queue for CLI control thread
workq_t g_workq_cli;

/**
 * 
 * 
 * @author martin (10/15/23)
 * @brief start
 * @param argc 
 * @param argv 
 * 
 * @return int 
 */
int main(int argc, char **argv) {
    int actorsPerLLCgroup = 0;
    int i, j;
    actorThreadContext_t *p_context;


    workq_init(&g_workq_cli);

    topo_init();
    if (topo_getNodeCnt() != 1) {
        printf("Error: only single NUMA node supported for now\n");
        exit(1);
    }
    // assume single node for now
    dir_init();
    actorsPerLLCgroup = topo_getCoresPerLLCgroup() * (topo_getSMTOn() + 1);
    printf("actorsPerLLCgroup %d\n", actorsPerLLCgroup);

    for (i = 0; i < topo_getLLCgroupsCnt(); i++) {
        for (j = 0; j < ((actorsPerLLCgroup -1)/2); j++ ){
            //create senders
            p_context = dir_getContext(0, i, -1, -1);
            if (p_context != NULL) {
                sprintf(p_context->name, "send_%d_%d", i, j);
                workq_init(&p_context->workq_in);
                pthread_create(&p_context->thread_id, NULL, th_send, (void *) p_context);

            }
        }

        for (j = 0; j < ((actorsPerLLCgroup -1)/2); j++ ){
            //create receivers
            p_context = dir_getContext(0, i, -1, -1);
            if (p_context != NULL) {
                sprintf(p_context->name, "recv_%d_%d", i, j);
                workq_init(&p_context->workq_in);
                pthread_create(&p_context->thread_id, NULL, th_recv, (void *) p_context);

            }
        }
    }
    while (1) {
    }

    return 0;
}




void *th_send(void *p_arg){
    actorThreadContext_t *this = (actorThreadContext_t*) p_arg;
    cpu_set_t           my_set;        /* Define your cpu_set bit mask. */
     msg_t                  msg;

   printf("Thread_%d PID %d %d %s\n", this->srcId, getpid(), gettid(),  this->name);


    CPU_ZERO(&my_set); 
    if (this->osId >= 0) {
        CPU_SET(this->osId, &my_set);
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    }


     while (1) {
         if(workq_read(&this->workq_in, &msg)){
            if(msg.cmd == CMD_CTL_INIT){
                break;
            }
         }
     }

     //init code here

     //printf("%s%d init now\n", this->name, this->instance);



    msg.cmd = CMD_CTL_READY;
    msg.src = this->srcId;
    msg.length = 0;
    if(workq_write(&g_workq_cli, &msg)){
        printf("%d q is full\n", this->srcId);
    }

    while (1){
    }
}


void *th_recv(void *p_arg){
    actorThreadContext_t *this = (actorThreadContext_t*) p_arg;
    cpu_set_t           my_set;        /* Define your cpu_set bit mask. */
     msg_t                  msg;

   printf("Thread_%d PID %d %d %s\n", this->srcId, getpid(), gettid(),  this->name);


    CPU_ZERO(&my_set); 
    if (this->osId >= 0) {
        CPU_SET(this->osId, &my_set);
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    }


     while (1) {
         if(workq_read(&this->workq_in, &msg)){
            if(msg.cmd == CMD_CTL_INIT){
                break;
            }
         }
     }

     //init code here

     //printf("%s%d init now\n", this->name, this->instance);



    msg.cmd = CMD_CTL_READY;
    msg.src = this->srcId;
    msg.length = 0;
    if(workq_write(&g_workq_cli, &msg)){
        printf("%d q is full\n", this->srcId);
    }

    while (1){
    }
}


