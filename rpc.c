#define _POSIX_C_SOURCE 200112L // from lecture slides
#define _MAX_NAME_LEN 1000 // chars a function name can be
#define _MAX_REQUESTS 1000 // the maximum requests the client can make in the queue
#define _MAX_FUNCTIONS 30
#define _BUFFER_SIZE 2048 // the size of the read buffer

#define TRUE 1
#define FALSE 0
#define FAIL -1

#define _RPC_FIND 101
#define _RPC_CALL 102

#include "rpc.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


/* the client state */
struct rpc_client {
    const char* addr;
    int port;
    unsigned int socket;
};

/* handle of a particular function */
struct rpc_handle {
    int index_num;
    char name[_MAX_NAME_LEN];
    rpc_data* (*handler)(rpc_data*);
};

/* the server state */
struct rpc_server {
    int port;
    unsigned int socket;
    struct sockaddr_in6 addr;
    int num_handlers;
    rpc_handle* handlers[_MAX_REQUESTS]; /* will be a ll */
};

/* the vars to pass in a thread */
struct vars {
    rpc_server* srv;
    int socket;
};

/* the function that serves one request */
// void* rpc_serve_one(void* thread_vars);

/* Initialises server state */
/* RETURNS: rpc_server* on success, NULL on error */
rpc_server *rpc_init_server(int port) {
    int server_sock_fd = -1, temp=-1, flag;
    struct sockaddr_in6 server_sock_addr;
    socklen_t client_addr_len;
    char str_addr[INET6_ADDRSTRLEN];
    char ch;
    rpc_server* server = (rpc_server*)malloc(sizeof(rpc_server));

    /* create the socket */
    server_sock_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock_fd == -1) {
        fprintf(stderr, "%s", "Socket could not be created\n");
        return NULL;
    }

    /* set options as per brief */
    int enable = 1;
    if (setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        return NULL;
    }
    server_sock_addr.sin6_family = AF_INET6;
	server_sock_addr.sin6_addr = in6addr_any;
	server_sock_addr.sin6_port = htons(port);

    /* bind address and socket together */
    temp = bind(server_sock_fd, (struct sockaddr*)&server_sock_addr, sizeof(server_sock_addr));
    if (temp == -1) {
        perror("bind()");
        fprintf(stderr, "%s", "Error, binding failed\n");
        return NULL;
    }

    /* create the server state */
    server->port = port;
    server->socket = server_sock_fd;
    server->addr = server_sock_addr;
    server->num_handlers = 0;

    /* start listening to client calls*/
    temp = listen(server->socket, _MAX_REQUESTS);
    if (temp == -1) {
        fprintf(stderr, "%s", "Error, initiation of listening failed\n");
        close(server->socket);
        exit(EXIT_FAILURE);
    }

    return server;
}

/* Registers a function (mapping from name to handler) */
/* RETURNS: -1 on failure */
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    // do I need a check for name length?
    rpc_handle* new_handle = (rpc_handle*)malloc(sizeof(rpc_handle)); // need to dealloc this
    strcpy(new_handle->name, name);
    new_handle->handler = handler;
    if (srv->num_handlers >= _MAX_REQUESTS) {
        fprintf(stderr, "Error, cannot register any more functions - out of space\n");
        return -1;
    } else {
        /* store it at the next available spot in handlers[] */
        int i = srv->num_handlers;
        new_handle->index_num = i;
        srv->handlers[i] = new_handle;
        if (srv->handlers[i] == NULL) {
            fprintf(stderr, "Error, function failed to register\n");
            return -1;
        } else {
            srv->num_handlers+=1;
            return srv->num_handlers;
        }
    }
    // will have to close all these somehow too - may in close_client()?
}

/* Start serving requests */
void rpc_serve_all(rpc_server *srv) {
    /* vars */
    int temp=-1, flag=FALSE, connection_sock_fd, index=-1;
    char *data_stream=NULL, *temp_data1, *temp_data2_len, *temp_data2;
    size_t data_len, offset=0;
    char buff[_MAX_NAME_LEN];
    socklen_t addr_len = sizeof(srv->addr);

    /* make the connection */
    connection_sock_fd = accept((srv->socket), (struct sockaddr*)&(srv->addr), &addr_len);
    if (connection_sock_fd == -1) {
            perror("accept()");
            close(srv->socket);
            exit(EXIT_FAILURE);
    }

    while(1) {
        /* read which flag was sent */
        temp = read(connection_sock_fd, &flag, sizeof(flag));
        if (temp == -1) {
            perror("serve.rpc_find.1: read()");
            close(connection_sock_fd);
        } else if (flag == FALSE) {
            fprintf(stderr, "Error, flag was not assigned on initial call\n");
        } else if (temp == 0) {
            // then wait for more requests, keep the server open
        } else {
            /* RPC_FIND */
            if (flag == _RPC_FIND) {
                /* read the name we're looking for */
                temp = read(connection_sock_fd, &buff, sizeof(buff));
                if (temp == -1) {
                    perror("serve.rpc_find.2: read()");
                    exit(EXIT_FAILURE);
                }
                
                /* find the name (if its in the array )*/
                flag = FALSE;
                for (int i=0; i<(srv->num_handlers); i++) {
                    if (!strcmp(buff, srv->handlers[i]->name)) {
                        flag = TRUE;
                        /* write that we found it */
                        temp = write(connection_sock_fd, &flag, sizeof(flag));
                        if (temp == -1) {
                            perror("serve.rpc_find.3: write()");
                            exit(EXIT_FAILURE);
                        }
                        /* write the handler's index number */
                        temp = write(connection_sock_fd, &(srv->handlers[i]->index_num), sizeof(srv->handlers[i]->index_num));
                        if (temp == -1) {
                            perror("serve.rpc_find.4: write()");
                            exit(EXIT_FAILURE);
                        }
                        flag = TRUE;
                    }
                }
                if (flag == FALSE) {
                    /* then signal that it failed */
                    flag = FALSE;
                    write(connection_sock_fd, &flag, sizeof(flag));
                }
            }

            /* RPC_CALL */
            if (flag == _RPC_CALL) {
                /* read the index handle we're given */
                temp = read(connection_sock_fd, &index, sizeof(index));
                if (temp == -1) {
                    exit(EXIT_FAILURE);
                }

                /* read the payload */
                rpc_data* client_payload = (rpc_data*)malloc(sizeof(rpc_data));
                temp = read(connection_sock_fd, &client_payload->data1, sizeof(client_payload->data1)); //may be memory conversion errors here!
                if (temp == -1) {
                    perror("serve.rpc_call.1: read()");
                    exit(EXIT_FAILURE);
                }
                temp = read(connection_sock_fd, &client_payload->data2_len, sizeof(client_payload->data2_len)); //may be memory conversion errors here!
                if (temp == -1) {
                    perror("serve.rpc_call.2: read()");
                    exit(EXIT_FAILURE);
                }
                client_payload->data2 = realloc(client_payload->data2, client_payload->data2_len);
                temp = read(connection_sock_fd, client_payload->data2, client_payload->data2_len); //may be memory conversion errors here!
                if (temp == -1) {
                    perror("serve.rpc_call.3: read()");
                    exit(EXIT_FAILURE);
                }

                /* call the handler, get the result */
                rpc_data *client_result = (rpc_data*)malloc(sizeof(client_payload));
                client_result = ((srv->handlers[index])->handler)(client_payload);
                free(client_payload->data2);
                free(client_payload);
                
                /* send the result back */
                temp = write(connection_sock_fd, &client_result->data1, sizeof(client_result->data1));
                if (temp == -1) {
                    perror("serve.rpc_call.4: write()");
                    exit(EXIT_FAILURE);
                }
                temp = write(connection_sock_fd, &client_result->data2_len, sizeof(client_result->data2_len));
                if (temp == -1) {
                    perror("serve.rpc_call.5: write()");
                    exit(EXIT_FAILURE);
                }
                temp = write(connection_sock_fd, &client_result->data2, client_result->data2_len);
                if (temp == -1) {
                    perror("serve.rpc_call.6: write()");
                    exit(EXIT_FAILURE);
                }
                free(client_result);
            }
        }
    }
}

// void* rpc_serve_one(void* thread_vars) {

// }

/* Initialises client state */
/* RETURNS: rpc_client* on success, NULL on error */
rpc_client *rpc_init_client(char *addr, int port) {
    
    /* create socket */
    int client_sock_fd = -1, temp =-1;
    struct sockaddr_in6 client_sock_addr;
    rpc_client* client = (rpc_client*)malloc(sizeof(rpc_client));

    /* create the socket */
    client_sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (client_sock_fd == -1) {
        perror("socket()");
        return NULL;
    }

    /* set up address */
    client_sock_addr.sin6_family = AF_INET6;
    client_sock_addr.sin6_port = htons(port);
    temp = inet_pton(AF_INET6, addr, &client_sock_addr.sin6_addr);
    if (temp == -1) {
        perror("inet_pton()");
        close(client_sock_fd);
        return NULL;
    } else if (temp == 0) {
        fprintf(stderr, "Invalid IP address:%s\n", addr);
        exit(EXIT_FAILURE);
    }

    /* try to connect to server */
    temp = connect(client_sock_fd, (struct sockaddr*)&client_sock_addr, sizeof(client_sock_addr));
    if (temp == -1) {
        perror("connect()");
        close(client_sock_fd);
        return NULL;
    }

    /* store the socket in the client state */
    client->addr = addr;
    client->port = port;
    client->socket = client_sock_fd;
    return client;
}

/* Finds a remote function by name - you give it the name, it finds it in client */
/* RETURNS: rpc_handle* on success, NULL on error */
/* rpc_handle* will be freed with a single call to free(3) */
rpc_handle *rpc_find(rpc_client *cl, char *name) {
    int flag = _RPC_FIND, temp, index;
    rpc_handle* found_handle = (rpc_handle*)malloc(sizeof(rpc_handle));
    if (found_handle == NULL) {
        fprintf(stderr, "Error, malloc on result failed\n");
        exit(EXIT_FAILURE);
    }
    /* tell server it's about to receive a RPC_FIND */
    temp = write(cl->socket, &flag, sizeof(flag));
    if (temp == -1) {
        perror("write()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }

    /* send the serve the name its looking for */
    temp = write(cl->socket, name, sizeof(name));
    if (temp == -1) {
        perror("write()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }

    /* receive a response */
    temp = read(cl->socket, &flag, sizeof(flag));
    if (temp == -1) {
        perror("read()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }
    if (flag == TRUE) {
        /* then a rpc_handle was found */
        temp = read(cl->socket, &index, sizeof(index));
        if (temp == -1) {
            perror("read()");
            close(cl->socket);
            exit(EXIT_FAILURE);
        }
        /* create a pseudo-handle */
        found_handle->index_num = index;
        strcpy(found_handle->name, name);
        found_handle->handler = NULL;
        return found_handle;
    } else {
        return NULL;
    }
}

/* Calls remote function using handle */
/* RETURNS: rpc_data* on success, NULL on error */
rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    int temp =-1, flag=_RPC_CALL;
    // char* data_stream;
    // size_t data_len, offset=0;

    /* send a RPC_CALL request to the server */
    temp = write(cl->socket, &flag, sizeof(flag));
    if (temp == -1) {
        perror("write()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }

    /* send the handle's index to server */
    temp = write(cl->socket, &(h->index_num), sizeof(h->index_num));
    if (temp == -1) {
        perror("write()");
        close(cl->socket);
        exit(EXIT_FAILURE); 
    }

    // /* create a stream out of the payload struct */
    // data_len = (payload->data2_len)+sizeof(payload->data1)+sizeof(payload->data2_len);
    // data_stream = (char*)malloc(data_len);

    // /* copy data1 into the start of the stream*/
    // memcpy(data_stream, &(payload->data1), sizeof(payload->data1));
    // offset += sizeof(payload->data1);
    //  /* copy data2_len into the stream */
    // memcpy(data_stream+offset, &(payload->data2_len), sizeof(payload->data2_len));
    // offset += sizeof(payload->data2_len);
    // /* copy data2 into the start+data1len part of the stream */
    // memcpy(data_stream+offset, (payload->data2), payload->data2_len);

    // /* tell the server how large the stream is */
    // temp = write(cl->socket, &data_len, sizeof(data_len));
    // if (temp == -1) {
    //     perror("write()");
    //     close(cl->socket);
    //     exit(EXIT_FAILURE); 
    // }

    // /* send the stream to the server */
    // printf("--Writing the data stream to server\n");
    // temp = write(cl->socket, &data_stream, data_len);
    // if (temp == -1) {
    //     perror("write()");
    //     close(cl->socket);
    //     exit(EXIT_FAILURE); 
    // }

    /* send the data across */
    temp = write(cl->socket, &payload->data1, sizeof(payload->data1));
    if (temp == -1) {
        perror("write()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }
    temp = write(cl->socket, &payload->data2_len, sizeof(payload->data2_len));
    if (temp == -1) {
        perror("write()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }
    temp = write(cl->socket, payload->data2, payload->data2_len);
    if (temp == -1) {
        perror("write()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }

    /* read the resulting payload */
    rpc_data* result_payload = (rpc_data*)malloc(sizeof(rpc_data));
    temp = read(cl->socket, &result_payload->data1, sizeof(result_payload->data1));
    if (temp == -1) {
        perror("read()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }
    temp = read(cl->socket, &result_payload->data2_len, sizeof(result_payload->data2_len));
    if (temp == -1) {
        perror("read()");
        close(cl->socket);
        exit(EXIT_FAILURE);
    }
    // result_payload->data2 = realloc(result_payload->data2, result_payload->data2_len);
    if (result_payload->data2_len == 0) {
        /* then data2 is NULL */
        result_payload->data2 = NULL;
    } else {
        /* otherwise read data 2*/
        temp = read(cl->socket, &result_payload->data2, result_payload->data2_len);
        if (temp == -1) {
            perror("read()");
            close(cl->socket);
            exit(EXIT_FAILURE);
        }
    }

    // temp = read(cl->socket, &data_len, sizeof(data_len));
    // if (temp == -1) {
    //     perror("b: read()");
    //     close(cl->socket);
    //     exit(EXIT_FAILURE);
    // }
    // data_stream = realloc(data_stream, data_len);
    // if (data_stream == NULL) {
    //     printf("Error, realloc failed\n");
    // }
    // temp = read(cl->socket, &payload->data1, sizeof(payload->data1));
    // if (temp == -1) {
    //     perror("c: read()");
    //     close(cl->socket);
    //     exit(EXIT_FAILURE);
    // } else {
    //     printf("--Successfully receieved the data stream in return\n");
    //     memcpy(&(payload->data1), data_stream, sizeof(payload->data1)); // may be a machine-based error here (size of int)
    //     memcpy(&(payload->data2_len), data_stream+sizeof(payload->data1), sizeof(payload->data2_len));
    //     memcpy(&(payload->data2), data_stream+sizeof(payload->data1)+sizeof(payload->data2_len), payload->data2_len);
    //     return payload;
    // }
    // }
    /* Otherwise, something went wrong */
    if (result_payload == NULL) {
        fprintf(stderr, "Error, rpc_call read failed to be executed\n");
        return NULL;
    }
    return result_payload;
}

/* Cleans up client state and closes client */
void rpc_close_client(rpc_client *cl) {
    /* need to close dealloc rpc client */
    int temp;
    temp = close(cl->socket);
    if (temp == -1) {
        perror("close()");
        cl->port = -1; //to ensure no mistakes
    }
    free(cl);
}

/* Frees a rpc_data struct */
void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}
