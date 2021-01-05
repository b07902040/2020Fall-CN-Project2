CC = g++
OPENCV =  `pkg-config --cflags --libs opencv`
PTHREAD = -pthread

RECEIVER = receiver.cpp
SERVER = server.cpp
AGENT = agent.cpp
REC = receiver
SER = server
AGE = agent

all: server receiver agent
  
server: $(SERVER)
	$(CC) $(SERVER) -o $(SER)  $(OPENCV) $(PTHREAD) 
receiver: $(RECEIVER)
	$(CC) $(RECEIVER) -o $(REC)  $(OPENCV) $(PTHREAD)
agent: $(AGENT)
	$(CC) $(AGENT) -o $(AGE)  $(OPENCV) $(PTHREAD)

.PHONY: clean

clean:
	rm $(REC) $(SER) $(AGE)
