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
//see header file above for comments

extern int schedInit(int i_reserved, int i_static);

/**
 * 
 * 
 * @author martin (32/26/22)
 * @brief start
 * @param argc 
 * @param argv 
 * 
 * @return int 
 */
int main(int argc, char **argv) {

    C_topology.init();      // scape sysfs to get topology information

    C_topology.display();

    schedInit(2, 16);

    printf("Main done\n");

    return 0;
}

