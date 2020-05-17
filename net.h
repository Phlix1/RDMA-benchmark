#include <infiniband/verbs.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
using namespace std;
#define FAILURE (1)
#define SUCCESS (0)
#define KEY_MSG_SIZE_GID (87) 
#define KEY_PRINT_FMT_GID "%04x:%06x:%08x:%016llx:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
/* Macro for allocating. */
#define ALLOCATE(var,type,size)                                     \
{ if((var = (type*)malloc(sizeof(type)*(size))) == NULL)        \
	{ fprintf(stderr," Cannot Allocate\n"); exit(1);}}
#define GET_STRING(orig,temp) 						            \
{ ALLOCATE(orig,char,(strlen(temp) + 1)); strcpy(orig,temp); }

struct omni_parameters {
    int port;
    char *ib_devname;
    char *servername;
    uint8_t ib_port;
    int gid_index;
    int sockfd;
};
struct omni_dest {
    int lid;
    int qpn;
    unsigned rkey;
    unsigned long long addr;
    union ibv_gid gid;
};
struct omni_context {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_cq *send_cq;
    struct ibv_cq *recv_cq;
    struct ibv_qp **qp;
    struct ibv_mr **mr;
    void **buf;
	struct ibv_sge *sge_list;
	struct ibv_sge *recv_sge_list;
    struct ibv_send_wr *wr;
    struct ibv_recv_wr *rwr;
    uint64_t buff_size;
};
struct omni_comm {
    struct omni_context *rdma_ctx;
    struct omni_parameters *rdma_params;

};
static int ethernet_server_connect(struct omni_comm *comm){
    struct addrinfo *res, *t;
    struct addrinfo hints;
    char *service;
    int n;
	int sockfd = -1, connfd;
	memset(&hints, 0, sizeof hints);
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
    if (asprintf(&service,"%d", comm->rdma_params->port) < 0) {
        fprintf(stderr, "Couldn't set service\n");
		return FAILURE;
	}
    int number = getaddrinfo(NULL,service,&hints,&res);
	if (number < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(number), comm->rdma_params->servername, comm->rdma_params->port);
		return FAILURE;
	} 
	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			n = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);
			if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}
    freeaddrinfo(res); 
	if (sockfd < 0) {
		fprintf(stderr, "Couldn't listen to port %d\n", comm->rdma_params->port);
		return FAILURE;
	}
    printf("listen on port %d\n", comm->rdma_params->port);
    listen(sockfd, 1);
    connfd = accept(sockfd, NULL, 0);
	if (connfd < 0) {
		fprintf(stderr, "server accept() failed\n");
		close(sockfd);
		return FAILURE;
	} 
	close(sockfd);
	comm->rdma_params->sockfd = connfd;
	return SUCCESS;    
}
static int ethernet_client_connect(struct omni_comm *comm){
    struct addrinfo *res, *t;
    struct addrinfo hints;
    char *service;
	int sockfd = -1;
	memset(&hints, 0, sizeof hints);
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
    if (asprintf(&service,"%d", comm->rdma_params->port) < 0) {
		return FAILURE;
	}
    int number = getaddrinfo(comm->rdma_params->servername,service,&hints,&res);
	if (number < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(number), comm->rdma_params->servername, comm->rdma_params->port);
		return FAILURE;
	} 
	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}
    freeaddrinfo(res);    
	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n",comm->rdma_params->servername,comm->rdma_params->port);
		return FAILURE;
	}
	comm->rdma_params->sockfd = sockfd;
	return SUCCESS;     
}
static int ethernet_read_keys(struct omni_dest *rem_dest, struct omni_comm *comm){
    char msg[KEY_MSG_SIZE_GID];
    char *pstr = msg, *term;
    char tmp[90];
    int i;
	if (read(comm->rdma_params->sockfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "ethernet_read_keys: Couldn't read remote address\n");
		return FAILURE;
	}
    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->lid = (int)strtol(tmp, NULL, 16); /*LID*/

    pstr += term - pstr + 1;
    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->qpn = (int)strtol(tmp, NULL, 16); /*QPN*/

	pstr += term - pstr + 1;
	term = strpbrk(pstr, ":");
	memcpy(tmp, pstr, term - pstr);
	tmp[term - pstr] = 0;
	rem_dest->rkey = (unsigned)strtoul(tmp, NULL, 16); /*RKEY*/    

	pstr += term - pstr + 1;
	term = strpbrk(pstr, ":");
	memcpy(tmp, pstr, term - pstr);
	tmp[term - pstr] = 0;
	rem_dest->addr = strtoull(tmp, NULL, 16); /*Address*/

	for (i = 0; i < 16; ++i) {
		pstr += term - pstr + 1;
		term = strpbrk(pstr, ":");
		memcpy(tmp, pstr, term - pstr);
		tmp[term - pstr] = 0;
		rem_dest->gid.raw[i] = (unsigned char)strtoll(tmp, NULL, 16);
	}

    return SUCCESS;
}
static int ethernet_write_keys(struct omni_dest *my_dest, struct omni_comm *comm){
    char msg[KEY_MSG_SIZE_GID];
	sprintf(msg,KEY_PRINT_FMT_GID, my_dest->lid, my_dest->qpn,
			my_dest->rkey, my_dest->addr,
			my_dest->gid.raw[0],my_dest->gid.raw[1],
			my_dest->gid.raw[2],my_dest->gid.raw[3],
			my_dest->gid.raw[4],my_dest->gid.raw[5],
			my_dest->gid.raw[6],my_dest->gid.raw[7],
			my_dest->gid.raw[8],my_dest->gid.raw[9],
			my_dest->gid.raw[10],my_dest->gid.raw[11],
			my_dest->gid.raw[12],my_dest->gid.raw[13],
			my_dest->gid.raw[14],my_dest->gid.raw[15]);
	if (write(comm->rdma_params->sockfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "Couldn't send local address\n");
		return FAILURE;
	}
    return SUCCESS; 
}

int ctx_hand_shake(struct omni_comm *comm, struct omni_dest *my_dest, struct omni_dest *rem_dest){
    if (comm->rdma_params->servername) {
        if(ethernet_write_keys(my_dest, comm)){
            fprintf(stderr," Unable to write to socket/rdam_cm\n");
            return FAILURE;
        }
        if(ethernet_read_keys(rem_dest, comm)){
            fprintf(stderr," Unable to read from socket/rdam_cm\n");
            return FAILURE;            
        }
    }
    else {
        if(ethernet_read_keys(rem_dest, comm)){
            fprintf(stderr," Unable to read from socket/rdam_cm\n");
            return FAILURE; 
        }
        if(ethernet_write_keys(my_dest, comm)){
            fprintf(stderr," Unable to write to socket/rdam_cm\n");
            return FAILURE;
        }
    }
    return SUCCESS;
}

struct ibv_device* ctx_find_dev(char **ib_devname)
{
	int num_of_device;
	struct ibv_device **dev_list;
	struct ibv_device *ib_dev = NULL;

	dev_list = ibv_get_device_list(&num_of_device);

	if (num_of_device <= 0) {
		fprintf(stderr," Did not detect devices \n");
		fprintf(stderr," If device exists, check if driver is up\n");
		return NULL;
	}

	if (!ib_devname) {
		fprintf(stderr," Internal error, existing.\n");
		return NULL;
	}

	if (!*ib_devname) {
		ib_dev = dev_list[0];
		if (!ib_dev) {
			fprintf(stderr, "No IB devices found\n");
			exit (1);
		}
	} else {
		for (; (ib_dev = *dev_list); ++dev_list)
			if (!strcmp(ibv_get_device_name(ib_dev), *ib_devname))
				break;
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", *ib_devname);
			return NULL;
		}
	}

	GET_STRING(*ib_devname, ibv_get_device_name(ib_dev));
	return ib_dev;
}

void alloc_ctx(struct omni_context *ctx, uint64_t datasize, int qpn){
    ALLOCATE(ctx->qp, struct ibv_qp*, qpn);
    ALLOCATE(ctx->mr, struct ibv_mr*, qpn);
    ALLOCATE(ctx->buf, void* , qpn);
	ALLOCATE(ctx->wr, struct ibv_send_wr, qpn);
	ALLOCATE(ctx->sge_list, struct ibv_sge,qpn);
    ctx->buff_size = datasize;
}

int ctx_init(struct omni_context *ctx, int qpn, int tx_depth, void *input, struct omni_comm *comm){
    int i;
	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		return FAILURE;
	}
    int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    ctx->buf[0] = input;
    ctx->mr[0] = ibv_reg_mr(ctx->pd, ctx->buf[0], ctx->buff_size, flags);
    for(i=1; i<qpn; i++) {
        ctx->mr[i] = ctx->mr[0];
        ctx->buf[i] = (void*)((char*)ctx->buf[0] + i*(ctx->buff_size/qpn));
    }
    ctx->send_cq = ibv_create_cq(ctx->context, tx_depth*qpn, NULL, NULL, 0);
	if (!ctx->send_cq) {
		fprintf(stderr, "Couldn't create CQ\n");
		return FAILURE;
	}
    struct ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    qp_init_attr.send_cq = ctx->send_cq;
    qp_init_attr.recv_cq = ctx->send_cq;
    qp_init_attr.cap.max_send_wr  = tx_depth;
    qp_init_attr.cap.max_recv_wr  = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.srq = NULL;
    qp_init_attr.sq_sig_all = 0;

	struct ibv_qp_attr attr;
	memset(&attr, 0, sizeof(struct ibv_qp_attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.port_num = comm->rdma_params->ib_port;
	attr.pkey_index = 0;
	attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	int qp_flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    int ret = 0;

    for(i=0; i<qpn; i++){
        //create QP
		ctx->qp[i] = ibv_create_qp(ctx->pd, &qp_init_attr);
	    if (!ctx->qp[i]) {
	    	fprintf(stderr, "Couldn't create QP\n");
	    	return FAILURE;
	    }
        //init QP
		ret = ibv_modify_qp(ctx->qp[i], &attr, qp_flags);
		if(ret){
		    fprintf(stderr, "Failed to modify QP to INIT, ret=%d\n",ret);
		    return FAILURE;			
		}
    }
    return SUCCESS;
}

int set_up_connection(struct omni_context *ctx, struct omni_comm *comm, struct omni_dest *my_dest, int qpn){
	union ibv_gid temp_gid;
	struct ibv_port_attr attr;
	int i;
	if(ibv_query_gid(ctx->context, comm->rdma_params->ib_port, comm->rdma_params->gid_index, &temp_gid)){
		fprintf(stderr, "Failed to query gid\n");
		return FAILURE;
	}
	if (ibv_query_port(ctx->context,comm->rdma_params->ib_port,&attr)){
		fprintf(stderr, "Failed to query port\n");
		return FAILURE;		
	}
    for(i=0; i<qpn; i++){
		my_dest[i].lid = attr.lid;
		my_dest[i].qpn = ctx->qp[i]->qp_num;
		my_dest[i].rkey  = ctx->mr[i]->rkey;
		my_dest[i].addr = (uintptr_t)ctx->buf[i];
		memcpy(my_dest[i].gid.raw,temp_gid.raw ,16);
	}
	return SUCCESS;
}

static int modify_qp_to_rtr(struct ibv_qp *qp, struct ibv_qp_attr *attr, struct omni_comm *comm, struct omni_dest *remote_dest, struct omni_dest *my_dest){
	int flags;
	attr->qp_state = IBV_QPS_RTR;
	attr->ah_attr.src_path_bits = 0;
    attr->ah_attr.port_num = comm->rdma_params->ib_port;
	attr->ah_attr.dlid = remote_dest->lid;
	attr->ah_attr.sl = 0;
	attr->ah_attr.is_global = 1;
	attr->ah_attr.grh.dgid = remote_dest->gid;
	attr->ah_attr.grh.sgid_index = comm->rdma_params->gid_index;
	attr->ah_attr.grh.hop_limit = 0xFF;
	attr->ah_attr.grh.traffic_class = 0;
    attr->path_mtu = IBV_MTU_1024;
	attr->dest_qp_num = remote_dest->qpn;
	attr->rq_psn = 0;
	attr->max_dest_rd_atomic = 1;
	attr->min_rnr_timer = 12;
	flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    if(ibv_modify_qp(qp, attr, flags)){
		fprintf(stderr, "failed to modify QP state to RTR\n");
		return FAILURE;
	}
	return SUCCESS;
}

static int modify_qp_to_rts(struct ibv_qp *qp, struct ibv_qp_attr *attr, struct omni_comm *comm, struct omni_dest *remote_dest, struct omni_dest *my_dest){
	int flags;
	attr->qp_state = IBV_QPS_RTS;
	attr->sq_psn = 0;
	attr->timeout =14;
	attr->retry_cnt = 7;
	attr->rnr_retry = 7;
    attr->max_rd_atomic  = 1;
	flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
			IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if(ibv_modify_qp(qp, attr, flags)){
		fprintf(stderr, "failed to modify QP state to RTS\n");
		return FAILURE;
	}
	return SUCCESS;
}

int ctx_connect(struct omni_context *ctx, struct omni_comm *comm, struct omni_dest *remote_dest, struct omni_dest *my_dest, int qpn){
	int i;
	struct ibv_qp_attr attr;
	for(i=0; i<qpn; i++){
		memset(&attr, 0, sizeof attr);
		modify_qp_to_rtr(ctx->qp[i], &attr, comm, &remote_dest[i], &my_dest[i]);
		memset(&attr, 0, sizeof attr);
		modify_qp_to_rts(ctx->qp[i], &attr, comm, &remote_dest[i], &my_dest[i]);
	}
	return SUCCESS;
}