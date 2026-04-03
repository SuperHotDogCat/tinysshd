CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -I/opt/homebrew/include -L/opt/homebrew/lib -lssl -lcrypto

all: server client

server: server.cpp
	$(CXX) $(CXXFLAGS) -o server server.cpp

client: client.cpp
	$(CXX) $(CXXFLAGS) -o client client.cpp

clean:
	rm -f server client
