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

extern int topo_init(void);

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

    topo_init();

    return 0;
}

