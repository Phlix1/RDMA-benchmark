#include "net.h"
#include <iostream>
using namespace std;
int main(){
    char *input;
    int i;
    int cycle_buffer = sysconf(_SC_PAGESIZE);
    uint64_t datasize=8192;
    int qpn = 1;
    int tx_depth = 128;
    posix_memalign(reinterpret_cast<void**>(&input), cycle_buffer, datasize);
    memset(input, 0, sizeof(char)*datasize);

    struct omni_comm comm;
    struct omni_context ctx;
    struct ibv_device *ib_dev = NULL;
    struct omni_dest *my_dest, *rem_dest;
    memset(&comm,0,sizeof(struct omni_comm));
    memset(&ctx,0,sizeof(struct omni_context));
    ALLOCATE((&comm)->rdma_params, struct omni_parameters, 1);
    memset((&comm)->rdma_params, 0, sizeof(struct omni_parameters));
    (&comm)->rdma_params->port = 19875;
    (&comm)->rdma_params->ib_port = 1;
    (&comm)->rdma_params->gid_index = 0;
    (&comm)->rdma_params->ib_devname = NULL;
    (&comm)->rdma_params->servername = NULL;
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
    if(ethernet_server_connect(&comm)){
		fprintf(stderr, " Couldn't connect client with socket\n");
		return FAILURE;        
    }
    ALLOCATE(rem_dest, struct omni_dest, qpn);
    memset(rem_dest, 0, sizeof(struct omni_dest)*qpn);
    ALLOCATE(my_dest, struct omni_dest, qpn);
    memset(my_dest, 0, sizeof(struct omni_dest)*qpn);

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
    //wait for client
	uint32_t *msg = (uint32_t*)malloc(sizeof(uint32_t)*16);
	memset(msg, 0, sizeof(uint32_t)*16);
	int tmp;
	while(1){
		int status = read((&comm)->rdma_params->sockfd, msg, sizeof(uint32_t)*16);
		if(status<0){
			cout<<"read error"<<endl;
			return FAILURE;
		}
	}
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