CXX_FLAGS = -I../tinyrpc -I../tinyrpc/asio --std=c++11 -O2 -g -DASIO_STANDALONE -DUSE_ASIO=1 -DUSE_ZMQ=0 -fpermissive -pthread

test: test.cpp ../tinyrpc/*.h
	g++ test.cpp -o test $(CXX_FLAGS)

clean:
	-rm test
