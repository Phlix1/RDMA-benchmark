server: server-multithread.c client-multithread.c
	cc server-multithread.c -o mserver -g -libverbs -pthread
	cc client-multithread.c -o mclient -g -libverbs -pthread
	g++ -O3 -fopenmp aggregator_tput.cc -o aggregator
clean:
	rm -rf ./*.o ./mclient
	rm -rf ./*.o ./mserver
	rm -rf ./*.o ./aggregator
