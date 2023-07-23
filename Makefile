CC=cc
RPC_SYSTEM=rpc.o

.PHONY: format all

all: rpc-server rpc-client

$(RPC_SYSTEM): rpc.c rpc.h
	$(CC) -c -o $@ $<

rpc-server: $(RPC_SYSTEM) server.a
	$(CC) -Wall -o $@ $^
 
rpc-client: $(RPC_SYSTEM) client.a
	$(CC) -Wall -o $@ $^

format:
	clang-format -style=file -i *.c *.h

clean:
	rm -f *.o