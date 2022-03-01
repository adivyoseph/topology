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

int init(void);
int display(void);

topology_t C_topology = {

    .init = &init,
    .display = &display,

};

// private functions
int f_getNumCores(void);
int f_getCoreData(int i_core);


/**
 * @brief initialise topology from proc and sysfs
 * 
 * @author Martin (26 Feb 2022)
 * 
 * @param void 
 * 
 * @return int 
 */
int init(void){
    int i_core;


    //determine n_sockets
    C_topology.i_cores = f_getNumCores();
    //populate topology

    C_topology.p_core_head = NULL;
    C_topology.p_core_tail = NULL;
    for (i_core = 0 ;i_core < C_topology.i_cores; i_core++) {
        f_getCoreData(i_core);
    }




    printf("Init done\n");
    return 0;
}

/**
 * @brief display topology 
 * 
 * @author Martin (26 Feb 2022)
 * 
 * @param void 
 * 
 * @return int 
 */
int display(void){

    int i, j;

    core_t   *p_core = C_topology.p_core_head;

    node_t   *p_node = C_topology.p_node_head;


    //llc_t   *p_llc; /// = C_topology.p_llc_head;

    printf("Display walk cores\n");
    while (p_core) {
        printf("%6s %d  %d %d\n", 
               p_core->c_name, 
               p_core->i_smtIndex, 
               p_core->p_llc->i_gIndex,
               ((node_t *)p_core->p_llc->vp_node)->i_gIndex);
        p_core = p_core->p_next;
    }

    printf("Display walk nodes %d\n", C_topology.i_nodes);
    while (p_node) {
        printf("%s\n", p_node->c_name);
        for (i = 0 ; i < p_node->i_llc_n; i++) {
            printf("\t%s\n", p_node->p_llcs[i]->c_name);
            for (j = 0; j < p_node->p_llcs[i]->i_core_n; j++) {
                printf("\t\t%s\n", ((core_t *)p_node->p_llcs[i]->vp_cores[j])->c_name);
            }
        }


        p_node = p_node->p_next;
    }

    return 0;
}



int f_getNumCores(void){
    int rc = 0;
    char *p_path = "/sys/devices/system/cpu/possible";
    FILE *fp;
    size_t size;
    char *s_line;
    int n;

    fp = fopen(p_path,"r");
    if (fp == NULL) {
        printf("error %s\n", p_path);
        return -1;
    }
    size = 0;
    n = getline(&s_line, &size, fp);  //includes nl
    s_line[n-1] = '\0';
    rc = atoi(&s_line[2]);
    printf("possible %s rc = %d\n", s_line, rc+1);
    fclose(fp);
    if (s_line) free(s_line);


    return rc+1;
}

core_t   *f_getPCore(int i_core){

    core_t   *p_core = C_topology.p_core_head;

    while (p_core) {
        if (p_core->i_gIndex == i_core) {
            //printf("f_getPCore(%d) found\n", i_core);
            break;
        }
        p_core = p_core->p_next;
    }

    return p_core;
}


llc_t   *f_getLlc(int i_llc){

    llc_t   *p_llc = C_topology.p_llc_head;

    while (p_llc) {
        if (p_llc->i_gIndex == i_llc) {
            //printf("f_getPCore(%d) found\n", i_core);
            break;
        }
        p_llc = p_llc->p_next;
    }

    return p_llc;
}


node_t   *f_getNode(int i_node){

    node_t   *p_node = C_topology.p_node_head;

    while (p_node) {
        if (p_node->i_gIndex == i_node) {
            //printf("f_getPCore(%d) found\n", i_core);
            break;
        }
        p_node = p_node->p_next;
    }

    return p_node;
}


int f_setNode(core_t *p_core, llc_t *p_llc){
    char s_path[128]; 
    int n;
    DIR *p_dir;
    struct dirent *p_dirEntry;

    node_t *p_node;

    sprintf(s_path, "/sys/devices/system/cpu/cpu%d", p_core->i_gIndex);
    //get parent node
    p_dir = opendir(s_path);
    if (p_dir != NULL) {
        while ((p_dirEntry = readdir(p_dir))) {
            //printf("nodes %d %s\n", i_nodes, p_nodeDir->d_name);
            if (!strncmp(p_dirEntry->d_name, "node", 4)) {
                //found node
                n = atoi(&p_dirEntry->d_name[4]);
                printf("\tnode %s %d\n", p_dirEntry->d_name, n);

                p_node = f_getNode(n);

                if (p_node == NULL) {
                    p_node = (node_t *) malloc(sizeof(node_t));
                    C_topology.i_nodes++;
                    if (C_topology.p_node_head == NULL) {
                        C_topology.p_node_head = p_node;
                    }
                    else {
                        C_topology.p_node_tail->p_next = p_node;
                    }
                    C_topology.p_node_tail = p_node;
                    p_node->p_next = NULL;
                    p_node->i_gIndex = n;
                    sprintf(p_node->c_name, "node%d", n);

                }
                p_llc->vp_node = p_node;
                p_node->p_llcs[p_node->i_llc_n] = p_llc;
                p_node->i_llc_n++;
                break;
            }
        }
        closedir(p_dir);
    }


    return 0;
}

int f_setLlc(core_t   *p_core){

//    int rc = 0;

    char s_path[128]; 
    FILE *fp;
    size_t size;
    char *s_line;
    int n, m;
//    char s_file[128];
    DIR *p_dir;
    struct dirent *p_dirEntry;

    llc_t    *p_llc;

    sprintf(s_path, "/sys/devices/system/cpu/cpu%d/cache", p_core->i_gIndex);
    p_dir = opendir(s_path);
    if (p_dir != NULL) {
        m = 0;
        while ((p_dirEntry = readdir(p_dir))) {
            //printf("nodes %d %s\n", i_nodes, p_nodeDir->d_name);
            if (!strncmp(p_dirEntry->d_name, "index", 5)) {
                //found node
                n = atoi(&p_dirEntry->d_name[5]);
                printf("\tllc %s %d\n", p_dirEntry->d_name, n);
                if (n > m) {
                    m = n;
                }
            }
        }
        closedir(p_dir);
        //go to index3
        //cache id is global
        sprintf(s_path, "/sys/devices/system/cpu/cpu%d/cache/index%d/id", p_core->i_gIndex, m);
        fp = fopen(s_path,"r");
        if (fp == NULL) {
            printf("error %s\n", s_path);
            return -1;
        }
        size = 0;
        n = getline(&s_line, &size, fp);  
        m = atoi(s_line);
        printf("\t llc id %d\n", m);
        fclose(fp);
        if (s_line) free(s_line);

        p_llc = f_getLlc(m);

        if (p_llc == NULL) {
            p_llc = (llc_t *) malloc(sizeof(llc_t));
            C_topology.i_llcs++;
            if (C_topology.p_llc_head == NULL) {
                C_topology.p_llc_head = p_llc;
            }
            else {
                C_topology.p_llc_tail->p_next = p_llc;
            }
            C_topology.p_llc_tail = p_llc;
            p_llc->p_next = NULL;
            p_llc->i_gIndex = m;
            sprintf(p_llc->c_name, "LLC%d", m);
            f_setNode(p_core, p_llc);
        }
        p_core->p_llc = p_llc;
        p_llc->vp_cores[p_llc->i_core_n] = p_core;
        p_llc->i_core_n++;

    }


    return 0;
}

int f_getCoreData(int i_core)
{
    int rc = 0;

    char s_path[128]; 
    FILE *fp;
    size_t size;
    char *s_line;
    int n, m;
//    char s_file[128];
    //DIR *p_dir;
    //struct dirent *p_dirEntry;

    //node_t   *p_node;
    //llc_t    *p_llc;
    core_t   *p_core;


    //printf("f_getCoreData(%d)\n", i_core);
    sprintf(s_path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", i_core);
    fp = fopen(s_path,"r");
    if (fp == NULL) {
        printf("error %s\n", s_path);
        return -1;
    }
    size = 0;
    n = getline(&s_line, &size, fp);  
    for (m = 0; m < n; m++) {
        if (s_line[m] ==',') {
            s_line[m] = 0;
            break;
        }
    }
    if (m < n) {
        C_topology.i_smt++;
        m = atoi(&s_line[m+1]);
    }
    else {
        m = -2;
    }
    n = atoi(s_line);


    if (i_core == n) {
        //first or only
        p_core = (core_t   *) malloc(sizeof(core_t));
        if (C_topology.p_core_head == NULL) {
            C_topology.p_core_head = p_core;
        }
        else {
            C_topology.p_core_tail->p_next = p_core;
        }
        C_topology.p_core_tail = p_core;
        p_core->p_next = NULL;
        p_core->i_gIndex = i_core;
        sprintf(p_core->c_name, "core%d", i_core);
        p_core->i_smtIndex = 0;

        f_setLlc(p_core);

    }
    if (m > 0) {
        //peer or duplicate
        p_core = f_getPCore(m);
        if (p_core == NULL) {
            p_core = (core_t   *) malloc(sizeof(core_t));
            C_topology.p_core_tail->p_next = p_core;

            C_topology.p_core_tail = p_core;
            p_core->p_next = NULL;
            p_core->i_gIndex = m;
            sprintf(p_core->c_name, "core%d", m);
            p_core->i_smtIndex = 1;


            f_setLlc(p_core);
        }
    }
    fclose(fp);
    if (s_line) free(s_line);






/*
    sprintf(s_path, "/sys/devices/system/cpu/cpu%d", i_core);
    //get parent node
    p_dir = opendir(s_path);
    if (p_dir != NULL) {
        while ((p_dirEntry = readdir(p_dir))) {
            //printf("nodes %d %s\n", i_nodes, p_nodeDir->d_name);
            if (!strncmp(p_dirEntry->d_name, "node", 4)) {
                //found node
                n = atoi(&p_dirEntry->d_name[4]);
                printf("\tnode %s %d\n", p_dirEntry->d_name, n);
                p_node = &C_topology.nodes[n];
               if (C_topology.i_nodes == 0) {
                    //C_topology.p_node_head = p_node;
                    C_topology.i_nodes = n +1;
                    //p_node->p_next = NULL;
                    //fill in node
                    p_node->i_gIndex = n;
                    strcpy(p_node->c_name, p_dirEntry->d_name);
                    p_node->i_llc_n = 0;
                }
                else if(n ==  C_topology.i_nodes){
                    C_topology.nodes[n -1].p_next = p_node;
                    C_topology.i_nodes = n +1;
                    //p_node->p_next = NULL;
                    //fill in node
                    p_node->i_gIndex = n;
                    strcpy(p_node->c_name, p_dirEntry->d_name);
                    p_node->i_llc_n = 0;
                }
                //else p_node already started
            }
        }
        closedir(p_dir);
    }
    //get llc index level (usually 3)
        //now get core info
        sprintf(s_path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", i_core);
        fp = fopen(s_path,"r");
        if (fp == NULL) {
            printf("error %s\n", s_path);
            return -1;
        }
        size = 0;
        n = getline(&s_line, &size, fp);  
        printf("\t siblings %s\n", s_line);
        for (m = 0; m < n; m++) {
            if (s_line[m] ==',') {
                s_line[m] = 0;
                break;
            }
        }
        n = atoi(s_line);
        if (m < n) {
            C_topology.i_smt++;
            m = atoi(&s_line[m+1]);
        }
        else {
            m = -1;
        }

        if (p_llc->i_core_n == 0) {
            //first core on this llc
            //also first parse
            C_topology.i_cores++;
            p_llc->i_core_n++
            p_core = &p_llc->core_list[0];
            p_core->i_gIndex = i_core;
            sprintf(p_core->c_name, "core%d", i_core);
            p_core->i_smtIndex = 0;
            if (m > 0) {
                C_topology.i_cores++;
                p_llc->i_core_n++
                p_core = &p_llc->core_list[1];
                p_core->i_gIndex = m;
                sprintf(p_core->c_name, "core%d", m);
                p_core->i_smtIndex = 1;
            }
        }
        else if( ){

        }


        fclose(fp);
        if (s_line) free(s_line);
    }


*/


    return rc;
}
