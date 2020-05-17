#include "net.h"
#include <time.h>
#include <iostream>
using namespace std;
int main(){
    char *input;
    int i;
    int cycle_buffer = sysconf(_SC_PAGESIZE);
    uint64_t datasize=4096;
    int qpn = 1;
    int tx_depth = 128;
    posix_memalign(reinterpret_cast<void**>(&input), cycle_buffer, datasize);
    memset(input, 0, sizeof(char)*datasize);
    
    struct omni_comm comm;
    struct omni_context ctx;
    struct ibv_device *ib_dev = NULL;
    memset(&comm,0,sizeof(struct omni_comm));
    memset(&ctx,0,sizeof(struct omni_context));
    struct omni_dest *my_dest, *rem_dest;
    ALLOCATE((&comm)->rdma_params, struct omni_parameters, 1);
    memset((&comm)->rdma_params, 0, sizeof(struct omni_parameters));
    (&comm)->rdma_params->port = 19875;
    char *servername;
    asprintf(&servername,"%s","11.0.0.210");
    (&comm)->rdma_params->servername = servername;
    (&comm)->rdma_params->ib_port = 1;
    (&comm)->rdma_params->gid_index = 0;
    (&comm)->rdma_params->ib_devname = NULL;
    ib_dev = ctx_find_dev(&(&comm)->rdma_params->ib_devname);
    ib_dev = ctx_find_dev(&(&comm)->rdma_params->ib_devname);
	if (!ib_dev) {
		fprintf(stderr," Unable to find the Infiniband/RoCE device\n");
		return FAILURE;
	}
    ctx.context = ibv_open_device(ib_dev);
	if (!ctx.context) {
		fprintf(stderr, " Couldn't get context for the device\n");
		return FAILURE;
	}
    if(ethernet_client_connect(&comm)){
		fprintf(stderr, " Couldn't connect server with socket\n");
		return FAILURE;        
    }
    ALLOCATE(rem_dest, struct omni_dest, 1);
    memset(rem_dest, 0, sizeof(struct omni_dest));
    ALLOCATE(my_dest, struct omni_dest, 1);
    memset(my_dest, 0, sizeof(struct omni_dest));

    alloc_ctx(&ctx, datasize, qpn);

    if(ctx_init(&ctx, qpn, tx_depth, (void*)input, &comm)){
			fprintf(stderr, " Couldn't create IB resources\n");
			return FAILURE;        
    }
	if (set_up_connection(&ctx, &comm, my_dest, qpn)) {
		fprintf(stderr," Unable to set up socket connection\n");
		return FAILURE;
	}

    for(i=0; i<qpn; i++){
		if (ctx_hand_shake(&comm,&my_dest[i],&rem_dest[i])) {
			fprintf(stderr," Failed to exchange data between server and clients\n");
			return FAILURE;
		}           
    }
	if (ctx_connect(&ctx,&comm, rem_dest, my_dest, qpn)) {
		fprintf(stderr," Unable to Connect the HCA's through the link\n");
		return FAILURE;
	}
	if (ctx_hand_shake(&comm,&my_dest[0],&rem_dest[0])) {
		fprintf(stderr," Failed to exchange data between server and clients\n");
		return FAILURE;
	}
    //test bw
	uint64_t totscnt = 0;
	uint64_t totccnt = 0;
    uint64_t *scnt;
    uint64_t *ccnt;
    struct timespec *tposted;
    struct timespec *tcompleted;
    uint64_t iters = 5000;
    uint64_t tot_iters=iters*qpn;
    int cq_mod = 10;
    i = 0; 
    int index, ne, wc_id;
    struct ibv_wc *wc = NULL;
    struct ibv_send_wr *bad_wr = NULL;
    ALLOCATE(wc ,struct ibv_wc ,16);
    ALLOCATE(scnt, uint64_t, qpn);
    ALLOCATE(ccnt, uint64_t, qpn);
    ALLOCATE(tposted, struct timespec, tot_iters);
    ALLOCATE(tcompleted, struct timespec, tot_iters);
    memset(scnt, 0, sizeof(uint64_t)*qpn);
    memset(ccnt, 0, sizeof(uint64_t)*qpn);
    memset(tposted, 0, sizeof(struct timespec)*tot_iters);
    memset(tcompleted, 0, sizeof(struct timespec)*tot_iters);    
    for(i=0; i<qpn; i++){
        memset(&((&ctx)->wr[i]),0,sizeof(struct ibv_send_wr));
        memset(&((&ctx)->wr[i]), 0, sizeof(ibv_sge));
        (&ctx)->sge_list[i].addr = (uintptr_t)(&ctx)->buf[i];
        (&ctx)->wr[i].wr.rdma.remote_addr   = rem_dest[i].addr;
        (&ctx)->sge_list[i].length = datasize/qpn;
        (&ctx)->sge_list[i].lkey = (&ctx)->mr[i]->lkey;
        (&ctx)->wr[i].sg_list = &((&ctx)->sge_list[i]);
        (&ctx)->wr[i].num_sge = 1;
        (&ctx)->wr[i].wr_id   = i;
        (&ctx)->wr[i].next = NULL;
        (&ctx)->wr[i].send_flags = IBV_SEND_SIGNALED;
        (&ctx)->wr[i].opcode = IBV_WR_RDMA_WRITE;
        (&ctx)->wr[i].wr.rdma.rkey = rem_dest[i].rkey;
    }
	uint32_t *msg = (uint32_t*)malloc(sizeof(uint32_t)*16);
    while(totscnt<tot_iters || totccnt<tot_iters){
        for(index=0; index<qpn; index++){
            while(scnt[index]<iters && (scnt[index]-ccnt[index]+1<=tx_depth)){
                if(scnt[index]%cq_mod==0 && cq_mod>1 && !(scnt[index] == (iters - 1))){
                    (&ctx)->wr[index].send_flags &= ~IBV_SEND_SIGNALED;
                }
                clock_gettime(CLOCK_REALTIME, &tposted[totscnt]);
                ibv_post_send((&ctx)->qp[index], &((&ctx)->wr[index]), &bad_wr);
                scnt[index] += 1;
                totscnt += 1;
                if((scnt[index]%cq_mod==cq_mod - 1)||scnt[index] == (iters - 1)){
                    (&ctx)->wr[index].send_flags |= IBV_SEND_SIGNALED;
                }

            }
        }
        if(totccnt<tot_iters) {
            ne = ibv_poll_cq((&ctx)->send_cq, 16, wc);
            if(ne>0) {
                memset(msg, 0, sizeof(uint32_t)*16);
                for(i=0; i<ne; i++){
                    wc_id = (int)wc[i].wr_id;
                    ccnt[wc_id] += cq_mod;
                    totccnt += cq_mod;
                    if(totccnt > tot_iters) clock_gettime(CLOCK_REALTIME, &tcompleted[tot_iters-1]);
                    else clock_gettime(CLOCK_REALTIME, &tcompleted[totccnt-1]);
                    msg[i] = totccnt;
                }
                write((&comm)->rdma_params->sockfd, msg, sizeof(uint32_t)*16);
            }
            else if (ne<0){
                fprintf(stderr, "poll CQ failed %d\n",ne);
                return FAILURE;
            }
        }
    }

    
	double seconds = (tcompleted[tot_iters-1].tv_sec - tposted[0].tv_sec) +
                       (tcompleted[tot_iters-1].tv_nsec - tposted[0].tv_nsec) / 1000000000.0;
    double tput = ((double)datasize)*8*iters/seconds/1000/1000/1000; 
    cout<<totscnt<<" "<<totccnt<<" "<<tput<<" Gbps"<<endl;
#ifdef DEBUG
    i=0;
        cout<<"local:"<<endl;
        printf(KEY_PRINT_FMT_GID, (&my_dest[i])->lid, (&my_dest[i])->qpn,
	    		(&my_dest[i])->rkey, (&my_dest[i])->addr,
	    		(&my_dest[i])->gid.raw[0],(&my_dest[i])->gid.raw[1],
	    		(&my_dest[i])->gid.raw[2],(&my_dest[i])->gid.raw[3],
	    		(&my_dest[i])->gid.raw[4],(&my_dest[i])->gid.raw[5],
	    		(&my_dest[i])->gid.raw[6],(&my_dest[i])->gid.raw[7],
	    		(&my_dest[i])->gid.raw[8],(&my_dest[i])->gid.raw[9],
	    		(&my_dest[i])->gid.raw[10],(&my_dest[i])->gid.raw[11],
	    		(&my_dest[i])->gid.raw[12],(&my_dest[i])->gid.raw[13],
	    		(&my_dest[i])->gid.raw[14],(&my_dest[i])->gid.raw[15]);
        cout<<endl; 
        cout<<"remote:"<<endl;    
        printf(KEY_PRINT_FMT_GID, (&rem_dest[i])->lid, (&rem_dest[i])->qpn,
	    		(&rem_dest[i])->rkey, (&rem_dest[i])->addr,
	    		(&rem_dest[i])->gid.raw[0],(&rem_dest[i])->gid.raw[1],
	    		(&rem_dest[i])->gid.raw[2],(&rem_dest[i])->gid.raw[3],
	    		(&rem_dest[i])->gid.raw[4],(&rem_dest[i])->gid.raw[5],
	    		(&rem_dest[i])->gid.raw[6],(&rem_dest[i])->gid.raw[7],
	    		(&rem_dest[i])->gid.raw[8],(&rem_dest[i])->gid.raw[9],
	    		(&rem_dest[i])->gid.raw[10],(&rem_dest[i])->gid.raw[11],
	    		(&rem_dest[i])->gid.raw[12],(&rem_dest[i])->gid.raw[13],
	    		(&rem_dest[i])->gid.raw[14],(&rem_dest[i])->gid.raw[15]);
        cout<<endl;  
#endif
}