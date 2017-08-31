USE_ASIO=1
# USE_ZMQ=0

CXXFLAGS = --std=c++14 -I../tinyrpc --std=c++14 -O0 -g -fpermissive -pthread
LDFLAGS = -lpthread -static-libstdc++ -static-libgcc

ifeq ($(USE_ASIO), 1)
	CXXFLAGS += -DUSE_ASIO=1 -DASIO_STANDALONE=1 -I../tinyrpc/asio/asio/include
endif

ifeq ($(USE_ZMQ), 1)
	CXXFLAGS += -DUSE_ZMQ=1 -I../tinyrpc/zmq
	LDFLAGS += -lzmq -L../tinyrpc/zmq
endif

test: test.cpp ../tinyrpc/*.h Makefile
	g++ test.cpp -o test $(CXXFLAGS) $(LDFLAGS)

clean:
	-rm test
