#include "utils.h"
#include <time.h>
#include "pthread.h"
typedef struct __DATA
{
    int     threadid;
    int    datasize;
    char*  operation;
}DATA;
void *run_server(void* param){
    DATA* data = (DATA*)param;
    struct resources res;
    int rc = 1;
    char temp_char;
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
    if (!strcmp(data->operation,"SEND") || !strcmp(data->operation,"WRITE_IMM") || !strcmp(data->operation,"SEND_IMM")){
		int times = 0;
		while(times<100){
			//printf("%d\n", times);
	        if (post_receive(&res, data->datasize))
	        {
	        	fprintf(stderr, "failed to post RR\n");
	        	goto main_exit;
	        }
			sock_sync_data(res.sock, 1, "R", &temp_char);
	        if (poll_completion(&res))
	        {
	        	fprintf(stderr, "poll completion failed\n");
	        	goto main_exit;
	        }
			times++;
		}
	}
    /* Sync so server will know that client is done mucking with its memory */
	if (sock_sync_data(res.sock, 1, "W", &temp_char)) /* just send a dummy char back and forth */
	{
		fprintf(stderr, "sync error after RDMA ops\n");
		rc = 1;
		goto main_exit;
	}
    fprintf(stdout, "Contents of server buffer: '%s'\n", res.buf);
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
    int threadnum = atoi(argv[1]);
	int datasize = atoi(argv[2]);
    tid = malloc(threadnum * sizeof *tid);
    
    DATA *data;
    data = malloc(threadnum * sizeof *data);
    for(int i=0; i<threadnum; i++){
        data[i].threadid = i;
        data[i].datasize = datasize;
        data[i].operation = argv[3];
        pthread_create(&tid[i], NULL, run_server, &data[i]);
    }
	printf("join finished\n");
    for(int i=0; i<threadnum; i++){
        pthread_join(tid[i],NULL);    
    }
	if (config.dev_name)
		free((char *)config.dev_name);	
    free(tid);
    free(data);
    return 0;
}