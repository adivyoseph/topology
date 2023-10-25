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
#define BILLION  1000000000L


extern int  menuInit();
extern int  menuLoop();
extern int  menuAddItem(char *name, int (*cbFn)(int, char *argv[]) , char *help);

int cbGetStats(int argc, char *argv[] );
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
                p_context->srcId = (p_context->node << 16) | (i << 8) | (p_context->core << 1) | p_context->cpu;
                //printf("srcId %x\n", p_context->srcId);
            }
            else{
                p_context->core = j;
                p_context->cpu = 0;
                p_context->osId = topo_getOsId(0, i,j,0);

                p_context->srcId = (p_context->node << 16) | (i << 8) | (j << 1);
            }

        }
    }
}

actorThreadContext_t *dir_getContext(int node, int llcgroup, int core, int cpu){
    actorThreadContext_t *p_context = NULL;
     int i;
    if (node == 0) {
        if (llcgroup < __nodes[node].llcGroupCnt) {
            if (core < __nodes[node].llcGroups[llcgroup].coreCnt) {
                if (topo_getSMTOn()) {
                    if (core == 0) {
                        i = cpu;
                    }
                    else
                    {
                        i = (core * 2) + cpu;
                    }
                    if (__nodes[node].llcGroups[llcgroup].cpus[i].state > 0) {
                        p_context = &__nodes[node].llcGroups[llcgroup].cpus[i].context;
                    }
                }
                else {
                    //ignore cpu
                    if (__nodes[node].llcGroups[llcgroup].cpus[core].state > 0) {
                        p_context = &__nodes[node].llcGroups[llcgroup].cpus[core].context;
                    }
                }
            }
        }
    }
    return p_context;
}


actorThreadContext_t *dir_setContext(int node, int llcgroup, int core, int cpu){
    actorThreadContext_t *p_context = NULL;
     int i;
    if (node == 0) {
        if (llcgroup < __nodes[node].llcGroupCnt) {
            if (core >= 0) {
                //TODO

            }
            else {
                if (cpu >= 0) {
                    //TODO
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



actorThreadContext_t *dir_findContext(int node, int llcgroup, int cpu, char *p_name){
    actorThreadContext_t *p_context = NULL;
    if (node == 0) {
        if (llcgroup < __nodes[node].llcGroupCnt) {
            if (cpu < __nodes[node].llcGroups[llcgroup].cpuCnt) {
               if (__nodes[node].llcGroups[llcgroup].cpus[cpu].state > 0) {
                   p_context = &__nodes[node].llcGroups[llcgroup].cpus[cpu].context;
                    if (strncmp(p_name, p_context->name, 4) == 0) {
                        //found one
                    }
                    else {
                        p_context = NULL;
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
    int c, i, j, k, l, m;
    actorThreadContext_t *p_context;
    msg_t                  msg;
    int totalReqs = 1000000;  // 1M
    int numSenders = -1;
    int numRecrs = -1;

    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    int cliAffinity = 1;

    unsigned cpu, numa;
    int opt;


    struct timespec start;
    struct timespec end;
    double accum;

    topo_init();

    menuInit();
    menuAddItem("s", cbGetStats, "get stats");

    while((opt = getopt(argc, argv, "hc:s:r:")) != -1) 
    { 
        switch(opt) 
        { 
        case 'h':                   //help
            printf("usage\n");
            return 0;
            break;


        case 'c':                    //cli cpu mapping
                cliAffinity = atoi(optarg);
                printf("cli cpu %d\n", cliAffinity); 
                break; 

        case 's':                    //ag cpu mapping
                i = atoi(optarg);
                j = ((topo_getCpusPerLLCgroup() -1)/2)*topo_getLLCgroupsCnt();
                if (i <= j) {
                    numSenders = i;
                    printf("number of senders %d\n", numSenders);
                }
                else {
                    printf("On this system max senders is %d (%d not valid)\n", j, i);
                    return 0;
                }
                break; 


        case 'r':                    //ag cpu mapping
                i = atoi(optarg);
                j = ((topo_getCpusPerLLCgroup() -1)/2)*topo_getLLCgroupsCnt();
                if (i <= j) {
                    numRecrs = i;
                    printf("number of receivers %d\n", numRecrs);
                }
                else {
                    printf("On this system max receivers is %d (%d not valid)\n", j, i);
                    return 0;
                }
                break; 
        default:
            break;
        }
    }

    if (numSenders < 0) {
        numSenders = 4;
        printf("number of senders %d (default)\n", numSenders);
    }
    if (numRecrs < 0) {
        numRecrs = numSenders;
        printf("number of receivers %d (default)\n", numRecrs);
    }


    getcpu(&cpu, &numa);
    printf("CLI %u %u\n", cpu, numa);

    CPU_ZERO(&my_set); 
    if (cliAffinity >= 0) {
        CPU_SET(cliAffinity, &my_set);
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    }

    getcpu(&cpu, &numa);
    printf("CLI %u %u\n", cpu, numa);
    workq_init(&g_workq_cli);

    if (topo_getNodeCnt() != 1) {
        printf("Error: only single NUMA node supported for now\n");
        exit(1);
    }
    // assume single node for now
    dir_init();
    actorsPerLLCgroup = (topo_getCpusPerLLCgroup() -1)/2;
    printf("actorsPerLLCgroup %d\n", actorsPerLLCgroup);

    for (i = 0; i < topo_getLLCgroupsCnt(); i++) {
        for (j = 0; j < actorsPerLLCgroup; j++ ){
            //create senders
            p_context = dir_setContext(0, i, -1, -1);
            if (p_context != NULL) {
                sprintf(p_context->name, "send_%d_%d", i, j);
                workq_init(&p_context->workq_in);
                pthread_create(&p_context->thread_id, NULL, th_send, (void *) p_context);
                //printf("context set llc %2d send %2d %s\n", i, j, p_context->name);

            }
        }

        for (j = 0; j < actorsPerLLCgroup; j++ ){
            //create receivers
            p_context = dir_setContext(0, i, -1, -1);
            if (p_context != NULL) {
                sprintf(p_context->name, "recv_%d_%d", i, j);
                workq_init(&p_context->workq_in);
                pthread_create(&p_context->thread_id, NULL, th_recv, (void *) p_context);
                //printf("context set llc %2d recv %2d %s\n", i, j, p_context->name);
            }
        }
    }
/* 
    for (i = 0, k = 0; i < topo_getLLCgroupsCnt(); i++) {
        for (j = 0; j < topo_getCpusPerLLCgroup(); j++) {
            printf("llc %2d dir_cpu %2d state %d  os_cpu %3d\n",
                   i, j,  __nodes[0].llcGroups[i].cpus[j].state, __nodes[0].llcGroups[i].cpus[j].context.osId);
        }
    }
 */
  // send init as all threads registered
  for (i = 0, k = 0; i < topo_getLLCgroupsCnt(); i++) {
      for (j = 0; j < topo_getCoresPerLLCgroup(); j++) {
          for (c = 0; c < 2; c++) {
              p_context = dir_getContext(0, i, j, c);
              if (p_context != NULL) {
                  msg.cmd = CMD_CTL_INIT;
                  msg.src = -1;
                  msg.length = 0;
                  if(workq_write(&p_context->workq_in, &msg)){
                      printf("%d q is full\n", p_context->srcId);
                  }
                  else{
                      k++;
                  }
              }
          }
      }
  }

  //wait for initialization to complete on all threads
  i = 0;
  while (1) {
         if(workq_read(&g_workq_cli, &msg)){
             if(msg.cmd == CMD_CTL_READY) {
                 i++;
                 //printf("thread %d is ready\n", msg.src);
             }
          if (i  >= k) {
              break;
          }
      }
  }
  printf("All threads ready\n");

  //send start

  l = totalReqs/numSenders;
  m = totalReqs - (i *(numSenders -1));  //first sender difference

  clock_gettime(CLOCK_REALTIME, &start);
  for (i = 0, k = 0; i < topo_getLLCgroupsCnt(); i++) {
      for (j = 0; j < topo_getCpusPerLLCgroup(); j++) {
          p_context = dir_findContext(0, i, j, "send");
          if (p_context != NULL) {
              msg.cmd = CMD_CTL_START;
              msg.src = -1;
              if (k == 0){
                  msg.length = m;
              }
              else {
                  msg.length = l;
              }
              msg.length = numSenders;
              msg.data = numRecrs;
              if(workq_write(&p_context->workq_in, &msg)){
                  printf("%d q is full\n", p_context->srcId);
              }
              k++;
              if (k >= numSenders) {
                  break;
              }
          }
      }
  }

  i = 0;
  while (1) {
      //wait for STOP
      if(workq_read(&g_workq_cli, &msg)){
           //printf("stop from %d \n", msg.src);
          if(msg.cmd == CMD_CTL_STOP) {
              i++;
              if (i == k) {
                  clock_gettime(CLOCK_REALTIME, &end);
                  break;
              }
          }
      }
     // menuLoop();
  }
  printf("finished\n");

  accum = ( end.tv_sec - start.tv_sec ) + (double)( end.tv_nsec - start.tv_nsec ) / (double)BILLION;
  printf( "%lf\n", accum );

  while (1) {
      if(menuLoop() == 0)  break;
  }

  return 0;
}


int cbGetStats(int argc, char *argv[] )
{

    return 0;
}

typedef struct th_dest_entry_s{
    int destId;
    int credits;
    workq_t *p_workq;
} th_dest_entry_t;

/**
 * 
 * 
 * @author martin (10/25/23) 
 *  
 * @brief request send thread 
 * 
 * @param p_arg  thread context
 * 
 * @return void* 
 */
void *th_send(void *p_arg){
    actorThreadContext_t *this = (actorThreadContext_t*) p_arg;
    cpu_set_t           my_set;        /* Define your cpu_set bit mask. */
     msg_t                  msg;
     int i, j, k;
     int th_destCnt = 0;
     th_dest_entry_t *p_th_destTable;
     actorThreadContext_t *p_context;
     int send_cnt = 0;
     int send_destCnt = 0;
     //workq_t *p_workq;


    CPU_ZERO(&my_set); 
    if (this->osId >= 0) {
        CPU_SET(this->osId, &my_set);
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    }

    printf("Thread_%06x PID %d %d cpu %3d %s\n", this->srcId, getpid(), gettid(), this->osId,  this->name);

     while (1) {
         if(workq_read(&this->workq_in, &msg)){
            if(msg.cmd == CMD_CTL_INIT){
                break;
            }
         }
     }

     //init code here
     i = topo_getLLCgroupsCnt() * topo_getCpusPerLLCgroup();

     p_th_destTable = malloc(i * sizeof(th_dest_entry_t));
     //populate table
     for (i = 0, k = 0; i < topo_getLLCgroupsCnt(); i++) {
         for (j = 0; j < topo_getCpusPerLLCgroup(); j++) {
             p_context = dir_findContext(0, i, j, "recv");
             if (p_context != NULL) {
                 p_th_destTable[k].credits = 4;
                 p_th_destTable[k].destId = p_context->srcId;
                 p_th_destTable[k].p_workq = &p_context->workq_in;
                 k++;
             }
         }
     }
     th_destCnt = k;


     //printf("%s%d init now\n", this->name, this->instance);



    msg.cmd = CMD_CTL_READY;
    msg.src = this->srcId;
    msg.length = 0;
    if(workq_write(&g_workq_cli, &msg)){
        printf("%d q is full\n", this->srcId);
    }

    // wait for start cmd
    while (1) {
        if(workq_read(&this->workq_in, &msg)){
           if(msg.cmd == CMD_CTL_START){
               send_cnt = msg.length;
               send_destCnt = msg.data;
               //printf("%s START send_cnt %d destCnt %d\n", this->name, send_cnt, send_destCnt);
               break;
           }
        }
    }
    j = 0;
    while (1) {
        //look for acks
        if(workq_read(&this->workq_in, &msg)){
            //assume ack for now
            for(i = 0; i < th_destCnt; i++){
                if (msg.src == p_th_destTable[i].destId) {
                    p_th_destTable[i].credits++;
                    break;
                }
            }
        }
        //now try to send something
        if (send_cnt > 0) {
            if (p_th_destTable[j].credits > 0) {
                //sent a request
                msg.cmd = CMD_REQ;
                msg.src = this->srcId;
                msg.length = send_cnt;
                if(workq_write(p_th_destTable[j].p_workq, &msg)){
                    this->errors++;
                }
                else {
                    //printf("%s sent REG to %08x %d\n", this->name, p_th_destTable[j].destId, send_cnt);
                    p_th_destTable[j].credits--;
                    send_cnt --;
                }
                j++;
                if (j >= send_destCnt) {
                    j = 0;
                }
            }
        }
        else {
            //check for outstanding acks

            for(i = 0; i < th_destCnt; i++){
                if(p_th_destTable[i].credits < 4) break;
            }
            if (i >= th_destCnt) {
                msg.cmd = CMD_CTL_STOP  ;
                msg.src = this->srcId;
                msg.length = 0;
                if(workq_write(&g_workq_cli, &msg)){
                    this->errors++;
                }
                else{
                    //printf("%s sent stop\n", this->name);
                    break;
                }
            }
        }
    }
    free(p_th_destTable);

    while (1){
    }

}

/**
 * 
 * 
 * @author martin (10/25/23) 
 *  
 * @brief receive thread 
 * 
 * @param p_arg 
 * 
 * @return void* 
 */
void *th_recv(void *p_arg){
    actorThreadContext_t *this = (actorThreadContext_t*) p_arg;
    cpu_set_t           my_set;        /* Define your cpu_set bit mask. */
     msg_t                  msg;
     int i, j, k;
     int th_destCnt = 0;
     th_dest_entry_t *p_th_destTable;
     actorThreadContext_t *p_context;
     workq_t *p_workq;


    CPU_ZERO(&my_set); 
    if (this->osId >= 0) {
        CPU_SET(this->osId, &my_set);
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    }

    printf("Thread_%06x PID %d %d cpu %3d %s\n", this->srcId, getpid(), gettid(), this->osId, this->name);

     while (1) {
         if(workq_read(&this->workq_in, &msg)){
            if(msg.cmd == CMD_CTL_INIT){
                break;
            }
         }
     }

     //init code here
     i = topo_getLLCgroupsCnt() * topo_getCpusPerLLCgroup();

     p_th_destTable = malloc(i * sizeof(th_dest_entry_t));
     //populate table
     for (i = 0, k = 0; i < topo_getLLCgroupsCnt(); i++) {
         for (j = 0; j < topo_getCpusPerLLCgroup(); j++) {
             p_context = dir_findContext(0, i, j, "send");
             if (p_context != NULL) {
                 //p_th_destTable[k]->credits = 4;
                 p_th_destTable[k].destId = p_context->srcId;
                 p_th_destTable[k].p_workq = &p_context->workq_in;
                 //printf("%s dest %2d %08x\n", this->name, k, p_th_destTable[k].destId);
                 k++;
             }
         }
     }
     th_destCnt = k;
     //printf("%s%d init now\n", this->name, this->instance);



    msg.cmd = CMD_CTL_READY;
    msg.src = this->srcId;
    msg.length = 0;
    if(workq_write(&g_workq_cli, &msg)){
        printf("%d q is full\n", this->srcId);
    }

    //ok to drop into running mode

    while (1){
        if(workq_read(&this->workq_in, &msg)){
            //printf("%s %08x RECV from %08x seq %d\n", this->name, this->srcId, msg.src, msg.length);
            //assume request for now
            for(i = 0; i < th_destCnt; i++){
                if (msg.src == p_th_destTable[i].destId) {
                    p_workq = p_th_destTable[i].p_workq;
                    msg.src = this->srcId;
                    msg.cmd = CMD_REQ_ACK;

                    if (workq_write(p_workq, &msg)) {
                        //printf("%d q is full\n", this->srcId);
                        this->errors++;
                    }
                    else {
                        //printf("%s %08x send ack to %08x seq %d\n", this->name, this->srcId, p_th_destTable[i].destId, msg.length);
                    }
                    break;
                }
            }
        }
    }

    free(p_th_destTable);
}


