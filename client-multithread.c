#include "utils.h"
#include <time.h>
typedef struct __DATA
{
    int     threadid;
    int    datasize;
    char*  operation;
	double *tput;
}DATA;
void *run_client(void* param){
    DATA* data = (DATA*)param;
    int opcode;
	if (!strcmp(data->operation,"SEND")){
		opcode = IBV_WR_SEND;
	}
	else if(!strcmp(data->operation,"READ")){
		opcode = IBV_WR_RDMA_READ;
	}
	else if(!strcmp(data->operation,"WRITE")){
		opcode = IBV_WR_RDMA_WRITE;
	}
	else if(!strcmp(data->operation,"WRITE_IMM")){
		opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
	}
	else if(!strcmp(data->operation,"SEND_IMM")){
		opcode = IBV_WR_SEND_WITH_IMM;
	}
	else{
		fprintf(stdout, "Unknown Request\n");		
	}
    struct resources res;
    int rc = 1;
    char temp_char;
    config.server_name = "11.0.0.210";
    //print_config();
    resources_init(&res);
	if(sock_init(&res, data->threadid))
	{
		fprintf(stderr, "failed to create socket\n");
		goto main_exit;
	}
    /* create resources before using them */
	if (resources_create(&res, data->datasize))
	{
		fprintf(stderr, "failed to create resources\n");
		goto main_exit;
	}
	/* connect the QPs */
	if (connect_qp(&res))
	{
		fprintf(stderr, "failed to connect QPs\n");
		goto main_exit;
	}
	/* Sync so we are sure server side has data ready before client tries to read it */
	if (sock_sync_data(res.sock, 1, "R", &temp_char)) /* just send a dummy char back and forth */
	{
		fprintf(stderr, "sync error before RDMA ops\n");
		rc = 1;
		goto main_exit;
	}
    struct timespec start, end;
	double rolling_iter;
	rolling_iter = 0;
	data->tput[data->threadid]  = 0;
	clock_gettime(CLOCK_REALTIME, &start);
	while(rolling_iter<100){
		if (!strcmp(data->operation,"SEND") || !strcmp(data->operation,"WRITE_IMM") || !strcmp(data->operation,"SEND_IMM")){
            sock_sync_data(res.sock, 1, "R", &temp_char);
		}
		clock_gettime(CLOCK_REALTIME, &start);
		
	    if (post_send(&res, opcode, data->datasize))
	    {
	    	fprintf(stderr, "failed to post SR 2\n");
	    	rc = 1;
	    	goto main_exit;
	    }
		
	    if (poll_completion(&res))
	    {
	    	fprintf(stderr, "poll completion failed 2\n");
	    	rc = 1;
	    	goto main_exit;
	    }
		if(rolling_iter>=0){
            clock_gettime(CLOCK_REALTIME, &end);
	        double seconds = (end.tv_sec - start.tv_sec) +
                               (end.tv_nsec - start.tv_nsec) / 1000000000.0;
			//printf("RDMA_WRITE_WITH_IMM operation Throughput: %lf OPS\n", rolling_iter/seconds);
			//printf("%d, %s Throughput: %lf Gbps, %lf s\n",data->datasize,  data->operation, ((double)data->datasize)*8/seconds/1000/1000/1000, seconds);
			data->tput[data->threadid] += ((double)data->datasize)*8/seconds/1000/1000/1000;
			//clock_gettime(CLOCK_REALTIME, &start);
		}
		rolling_iter++;
	}
	data->tput[data->threadid] = data->tput[data->threadid]/rolling_iter;
	/* Sync so server will know that client is done mucking with its memory */
	if (sock_sync_data(res.sock, 1, "W", &temp_char)) /* just send a dummy char back and forth */
	{
		fprintf(stderr, "sync error after RDMA ops\n");
		rc = 1;
		goto main_exit;
	}
	rc = 0;
main_exit:
	if (resources_destroy(&res))
	{
		fprintf(stderr, "failed to destroy resources\n");
		rc = 1;
	}
	//if (config.dev_name)
	//	free((char *)config.dev_name);
	fprintf(stdout, "\ntest result is %d\n", rc);
}
int main(int argc, char *argv[]){
	if( argc < 4 )
    {
        printf("No input. 1: threadnum, 2: datasize(B),3: RDMA operation (READ|WRITE|WRITE_IMM|SEND|SEND_IMM)\n");
		return 0;
    }
    pthread_t *tid;
	double *tput;
    int threadnum = atoi(argv[1]);
	int datasize = atoi(argv[2]);

    tid = malloc(threadnum * sizeof *tid);
    
    DATA *data;
    data = malloc(threadnum * sizeof *data);
	tput = malloc(threadnum * sizeof *tput);
    for(int i=0; i<threadnum; i++){
        data[i].threadid = i;
        data[i].datasize = datasize;
        data[i].operation = argv[3];
		data[i].tput = tput;
        pthread_create(&tid[i], NULL, run_client, &data[i]);
    }
	printf("join finished\n");
    for(int i=0; i<threadnum; i++){
        pthread_join(tid[i],NULL);    
    }
	double total_tput = 100000000;
	for (int i=0;i<threadnum;i++){
        total_tput =total_tput<tput[i]?total_tput:tput[i];
		printf("thread %d : %lf Gbps\n", i, tput[i]);
	}
	total_tput *= threadnum;
	printf("total thoughput: %lf Gbps\n", total_tput);
	if (config.dev_name)
		free((char *)config.dev_name);
    free(tid);
    free(data);
    return 0;
}