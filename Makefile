server: testclient.cc testserver.cc
	g++ server.cc -o server -g -libverbs -pthread
	g++ client.cc -o client -g -libverbs -pthread
clean:
	rm -rf ./*.o ./client
	rm -rf ./*.o ./server
