CXX_FLAGS = -I ./asio --std=c++11 -O0 -g -DASIO_STANDALONE -fpermissive -pthread

test:
	g++ test.cpp logging.cpp -o test $(CXX_FLAGS)

clean:
	-rm test
