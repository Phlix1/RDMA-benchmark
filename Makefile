server: server-multithread.c client-multithread.c
	cc server-multithread.c -o mserver -g -libverbs -pthread
	cc client-multithread.c -o mclient -g -libverbs -pthread
clean:
	rm -rf ./*.o ./mclient
	rm -rf ./*.o ./mserver
