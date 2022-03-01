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

static int init(void);
static int display(void);
static int smtEnabled(void);

topology_t C_topology = {

    .init = &init,
    .display = &display,
    .smtEnabled = &smrEnabled,

};

// private helper functions
static int f_getNumCores(void);
static int f_getCoreData(int i_core);
static core_t   *f_getPCore(int i_core);
static llc_t    *f_getLlc(int i_llc);
static node_t   *f_getNode(int i_node);
static int       f_setNode(core_t *p_core, llc_t *p_llc);
static int       f_setLlc(core_t   *p_core);

/**
 * @brief initialise topology from proc and sysfs
 * 
 * @author Martin (26 Feb 2022)
 * 
 * @param void 
 * 
 * @return int not used
 */
static int init(void){
    int i_core;             //local iterator variable
    //determine the number cores
    //sysfs topology uses core as the primary index
    C_topology.i_cores = f_getNumCores();
    //populate topology structure
    C_topology.p_core_head = NULL;
    C_topology.p_core_tail = NULL;
    for (i_core = 0 ;i_core < C_topology.i_cores; i_core++) {
        f_getCoreData(i_core);
    }
    return 0;
}

/**
 * @brief display topology 
 * 
 * @author Martin (26 Feb 2022)
 * 
 * @param void 
 * 
 * @return int not used
 */
int display(void){

    int i, j;

    core_t   *p_core = C_topology.p_core_head;
    node_t   *p_node = C_topology.p_node_head;

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


/**
 * @brief use sysfs to get core count
 * 
 * @author root (1 Mar 2022)
 * 
 * @return int number of cores (including SMT)
 */
static int f_getNumCores(void){
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
    //printf("possible %s rc = %d\n", s_line, rc+1);
    fclose(fp);
    if (s_line) free(s_line);


    return rc+1;
}

/**
 * @brief locate core object instance by core index
 * 
 * @author root (1 Mar 2022)
 * 
 * @param i_core 
 * 
 * @return core_t* pointer to core object or NULL if not found
 */
static core_t   *f_getPCore(int i_core){
    core_t   *p_core = C_topology.p_core_head;

    while (p_core) {
        if (p_core->i_gIndex == i_core) {
            break;
        }
        p_core = p_core->p_next;
    }

    return p_core;
}

/**
 * @brief loacate LLC (Late level cache) by index
 * 
 * @author root (1 Mar 2022)
 * 
 * @param i_llc 
 * 
 * @return llc_t* llc object or NULL if not found
 */
static llc_t   *f_getLlc(int i_llc){
    llc_t   *p_llc = C_topology.p_llc_head;

    while (p_llc) {
        if (p_llc->i_gIndex == i_llc) {
            break;
        }
        p_llc = p_llc->p_next;
    }

    return p_llc;
}

/**
 * @brief locate node object instance by index
 * 
 * @author root (1 Mar 2022)
 * 
 * @param i_node 
 * 
 * @return node_t* node object or NULL if not found
 */
static node_t   *f_getNode(int i_node){
    node_t   *p_node = C_topology.p_node_head;

    while (p_node) {
        if (p_node->i_gIndex == i_node) {
            break;
        }
        p_node = p_node->p_next;
    }

    return p_node;
}

/**
 * @brief link llc to parent node and create it if it does not 
 *        exist. Create back pointer from node to child llc.
 *        called when a new llc object is created 
 * 
 * @author root (1 Mar 2022)
 * 
 * @param p_core contains core global index, used to look up 
 *               parent node index
 * @param p_llc llc to associate with node
 * 
 * @return int not used
 */
static int f_setNode(core_t *p_core, llc_t *p_llc){
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
                //printf("\tnode %s %d\n", p_dirEntry->d_name, n);

                p_node = f_getNode(n);  //see if node already exits

                if (p_node == NULL) {
                    //first time create node
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
                    sprintf(p_node->c_name, "node%d", n);  //used for debug and display only
                }
                p_llc->vp_node = p_node;  //set llc parent node
                //sysfs enumeration is in order so the following is a safe assumption
                //add child pointer for this llc
                p_node->p_llcs[p_node->i_llc_n] = p_llc;
                p_node->i_llc_n++;
                break;
            }
        }
        closedir(p_dir);
    }


    return 0;
}

/**
 * @brief get core llc index (global id) 
 *        create if required, also adds node if required
 *        create llc to core back linkages 
 * 
 * @author root (1 Mar 2022)
 * 
 * @param p_core 
 * 
 * @return int not used
 */
static int f_setLlc(core_t   *p_core){
    char s_path[128]; 
    FILE *fp;
    size_t size;
    char *s_line;
    int n, m;
    DIR *p_dir;
    struct dirent *p_dirEntry;

    llc_t    *p_llc;

    sprintf(s_path, "/sys/devices/system/cpu/cpu%d/cache", p_core->i_gIndex);
    p_dir = opendir(s_path);
    if (p_dir != NULL) {
        m = 0;
        while ((p_dirEntry = readdir(p_dir))) {
            if (!strncmp(p_dirEntry->d_name, "index", 5)) {
                n = atoi(&p_dirEntry->d_name[5]);
                if (n > m) {
                    m = n;  //m track max cache index, depth
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
        m = atoi(s_line);                   //m now contains llc global id
        //printf("\t llc id %d\n", m);
        fclose(fp);
        if (s_line) free(s_line);

        p_llc = f_getLlc(m);  //see if llc instance already exits

        if (p_llc == NULL) {
            //new llc required
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
            f_setNode(p_core, p_llc);  // add node if required and create linkages
        }
        p_core->p_llc = p_llc;          //associate core with this llc
        //since syfs is parsed in order the following works
        p_llc->vp_cores[p_llc->i_core_n] = p_core;  //add llc to core back link
        p_llc->i_core_n++;
    }

    return 0;
}


/**
 * @brief using core index add per core tolopogy data
 * 
 * @author root (1 Mar 2022)
 * 
 * @param i_core 
 * 
 * @return int 
 */
static int f_getCoreData(int i_core)
{
    int rc = 0;
    char s_path[128]; 
    FILE *fp;
    size_t size;
    char *s_line;
    int n, m;
    core_t   *p_core;
    int i_smts[8];          //assume max smt of 8 for now
    int i_smt = 0;


    sprintf(s_path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", i_core);
    fp = fopen(s_path,"r");
    if (fp == NULL) {
        printf("error %s\n", s_path);
        return -1;
    }
    size = 0;
    n = getline(&s_line, &size, fp); 
    //line msy be a list of 1 or more core indexs (SMT 2,4,8)
    //split line string 
    i_smts[i_smt] = 0;
    i_smt++;

    for (m = 0; m < n; m++) {
        if (s_line[m] ==',') {
            s_line[m] = 0;
            i_smts[i_smt] = m+1;
            i_smt++;
            C_topology.i_smt = 1;
        }
    }

    // add all cores the first time through
    // need smt order
    for (m = 0; m < i_smt; m++) {
        n = atoi(&s_line[i_smts[m]]);   //get core index
        p_core = f_getPCore(n);         //see if it already exits
        if (p_core == NULL) {
            p_core = (core_t   *) malloc(sizeof(core_t));
            if (C_topology.p_core_head == NULL) {
                C_topology.p_core_head = p_core;
            }
            else {
                C_topology.p_core_tail->p_next = p_core;
            }
            C_topology.p_core_tail = p_core;
            p_core->p_next = NULL;
            p_core->i_gIndex = n;
            sprintf(p_core->c_name, "core%d", n);
            p_core->i_smtIndex = m;
            f_setLlc(p_core);
        }
    }

    fclose(fp);
    if (s_line) free(s_line);


    return rc;
}


/**
 * @brief see if smt is enabled
 * 
 * @author root (1 Mar 2022)
 * 
 * @param void 
 * 
 * @return int 0 not enabled, 1 enabled
 */
static int smtEnabled(void){
    return C_topology.i_smt;
}
