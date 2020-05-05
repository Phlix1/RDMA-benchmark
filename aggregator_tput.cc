#include <memory>
#include <cstdlib> 
#include <iostream>
#include <vector>
#include <algorithm> 
#include <ctime>
#include <chrono>
#include <thread>
#include <omp.h>
//#define DEBUG
void generate_input(int workernum, int datasize, int blocksize, float density, int* &bnum, int **&indexs, float **&values){
    values = (float**)malloc(sizeof(float*)*workernum);
    indexs = (int**)malloc(sizeof(int*)*workernum);
    bnum = (int*)malloc(sizeof(int)*workernum);
    int blocknum = (int)(datasize/blocksize);
    int selblocknum = (int)(blocknum*density);
    std::srand (unsigned(std::time(0)));
    for(int i=0; i<workernum; i++){
        std::vector<int> shuffle_indexs;
        for(int j=0; j<blocknum; j++) shuffle_indexs.push_back(j);
        std::random_shuffle (shuffle_indexs.begin(), shuffle_indexs.end());
        bnum[i] = selblocknum;
        indexs[i] = (int*)malloc(sizeof(int)*selblocknum);
        int tmp= posix_memalign(reinterpret_cast<void**>(&values[i] ), 64, sizeof(float)*selblocknum*blocksize);
        for(int k=0; k<selblocknum*blocksize; k++) values[i][k] = 0.01;
        tmp = 0;
        std::sort(shuffle_indexs.begin(), shuffle_indexs.begin()+selblocknum);   
        for (std::vector<int>::iterator it=shuffle_indexs.begin(); it!=shuffle_indexs.end()&&tmp<selblocknum; ++it){
            indexs[i][tmp] = *it;
            tmp++;
        }
    }
#ifdef DEBUG
    for(int i=0; i<workernum; i++){
        std::cout<<"Worker "<<i<<" nonzero block number "<<bnum[i]<<std::endl;
        for(int j=0; j<bnum[i]; j++){
            std::cout<<indexs[i][j]<<" : ";
            for(int k=0; k<blocksize; k++){
                std::cout<<values[i][j*blocksize+k]<<", ";
            }
            std::cout<<std::endl;
        }
        std::cout<<std::endl;
    }
#endif
    return;
}

void aggregation(int workernum, int* bnum, int blocksize, int **indexs, float **values, float *&result){
    int *ptr = (int*)malloc(sizeof(int)*workernum);
    for(int i=0; i<workernum; i++) ptr[i] = 0;
    bool finished = false;
    while(!finished){
        int minindex=INT32_MAX;
        for(int i=0; i<workernum; i++){
            if (indexs[i][ptr[i]]<minindex && ptr[i]<bnum[i]) minindex=indexs[i][ptr[i]];
        }
        for(int i=0; i<workernum; i++){
            if (indexs[i][ptr[i]]==minindex){
                for(int j=0; j<blocksize; j++){
                    result[minindex*blocksize+j] += values[i][ptr[i]*blocksize+j];
                }
                ptr[i]++;
            }
        }
        finished = true;
        for(int i=0; i<workernum; i++){
            if(ptr[i]<bnum[i]){
                finished=false;
                break;
            }
        }
    }
}

int main(int argc, char *argv[]){
    if( argc < 5 )
    {
        printf("No input. 1: workernum, 2: threadnum, 3:blocksize, 4: datasize, 5: density \n");
		return 0;
    }
    int workernum = atoi(argv[1]);
    int threadnum = atoi(argv[2]);
    int blocksize = atoi(argv[3]);
    int datasize = (atoi(argv[4])/blocksize/threadnum)*blocksize*threadnum;
    
    int blocknum = datasize/blocksize;
    float density = atof(argv[5]);


    float ***values = (float***)malloc(sizeof(float**)*threadnum);
    int ***indexs = (int***)malloc(sizeof(int**)*threadnum);
    int **bnum = (int**)malloc(sizeof(int*)*threadnum);
    float *result;
    for(int i=0; i<threadnum; i++){
        generate_input(workernum, datasize/threadnum, blocksize, density, bnum[i], indexs[i], values[i]);
    }
    int tmp = posix_memalign(reinterpret_cast<void**>(&result), 64, sizeof(float)*datasize);
    for(int i=0; i<datasize; i++){
        result[i]=0.0;
    }
    float **result_thread = (float**)malloc(sizeof(float*)*threadnum);
    for(int i=0; i<threadnum; i++){
        result_thread[i]=result+i*(datasize/threadnum);
    }
    float microseconds=0;
    float tput=0;
    int num_rounds = 100;
    for(int roundnum = 0; roundnum<num_rounds; roundnum++){
        for(int i=0; i<datasize; i++){
            result[i]=0.0;
        }        
        auto begin = std::chrono::high_resolution_clock::now();
        #pragma omp parallel for num_threads(threadnum)
        for(int i=0; i<threadnum; i++){
            aggregation(workernum, bnum[i], blocksize, indexs[i], values[i], result_thread[i]);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto tmptime = (float)std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
        microseconds += tmptime;
        tput += float((long long)(datasize)*4*8*(long long)(workernum))/tmptime/1000;
    }
#ifdef DEBUG
    for(int i=0; i<datasize; i++){
        std::cout<<result[i]<<" ";
    }
    std::cout<<std::endl;
#endif
    std::cout<<"number of threads: "<<threadnum<<"; data size: "<<datasize<<"; throughput: "<<tput/num_rounds<< "Gbps;"<<" time: "<<microseconds/num_rounds<<std::endl;
    
    for(int i=0; i<threadnum; i++){
        free(bnum[i]);
        for(int j=0; j<workernum; j++){
            free(indexs[i][j]);
            free(values[i][j]);
        }
    }
    free(result);
    return 0;
}