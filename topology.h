#ifndef ____TOPOLOGY_H____
#define ____TOPOLOGY_H____

/**
 * Topology comes from /sys/devices/system/node 
 * node stands for NUMA node, numbered 0,1,2,... 
 * NUMA and socket have no direct relationship 
 * A socket may contain 1 or more NUMA nodes 
 * CPU NUMA nodes generally have memory and IO (PCIe RC) as well 
 * NUMA should only be used for managing memory domains and not 
 * Core groupings 
 * A CPU NUMA node may have 1 or more Last Level Cache (LLC) 
 * Core affinity groups 
 * NUMA cannot be used to map or group Core afinity, peering 
 * (only memory and IO) 
 * SMT support is automatic, support for up to smt 8 
 *  
 */

#define STR_MAX             32
#define LLC_PER_NUMA_MAX    16
#define CORE_PER_LLC_MAX    256
#define NODE_MAX            64



typedef struct llc_s {
    struct llc_s    *p_next;     // global single linked list of LLC's
    struct llc_s    *p_peer;     // per node single linked list of LLC's
    int             i_gIndex;   // system wide LLC index
    char            c_name[STR_MAX]; // text name for display and debug use only
    int             i_core_n;       // count of of SW/OS visible Cores for this llc_list entry
    void            *vp_cores[CORE_PER_LLC_MAX];
    void            *vp_node;
} llc_t;


typedef struct core_s {
    struct core_s    *p_next;     // global single linked list of core's
    struct core_s    *p_peer;     // per LLC single linked list of core's
    llc_t            *p_llc;      // parent llc
    int             i_smtIndex;  // 0 only no SMT, 0 or 1 with SMT
    int             i_gIndex;   // system wide core index
    char            c_name[STR_MAX]; // text name for display and debug use only
    // topology manager state
    int             i_pool;         // pools, reserved, static, shared
    int             i_state;        // free, reserved, allocated
    int             i_share_cnt;    // when shared
} core_t;

/**
 * @brief NUMA node instance object 
 *       src /sys/devices/system/node 
 * 
 * @author root (26 Feb 2022)
 */
typedef struct node_s {
    struct node_s *p_next;          // single linked list
    int            i_gIndex;        // system wide NUMA (NODE) Index
    char           c_name[STR_MAX]; // text name for display and debug use only
    struct llc_s   *p_llcs[LLC_PER_NUMA_MAX];
    int            i_llc_n;         // count of valid llc_list entries, always sequential
} node_t;





typedef struct topology_s {

    int      i_nodes;
    struct node_s *p_node_head;
    struct node_s *p_node_tail;
    struct node_s *p_node_itr;

    int      i_llcs;
    struct llc_s    *p_llc_head;
    struct llc_s    *p_llc_tail;
    struct llc_s    *p_llc_itr;
    struct llc_s    *p_llc_itr_peer;

    int      i_cores;
    struct core_s   *p_core_head;
    struct core_s   *p_core_tail;
    struct core_s   *p_core_itr;
    struct core_s   *p_core_itr_peer;
    int      i_smt;


    int (*init)(void);
    int (*display)(void);


} topology_t;

extern topology_t C_topology;

#endif
