#include "docker.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h> 

static int docker_connect() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/var/run/docker.sock", sizeof(addr.sun_path));

    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        close(sock);
        return -1;
    }
    return sock;
}


int docker_create(const char *image,const char *username, const char *cmd, char **container_id){
    int sock = docker_connect();
    char request[1024];
    char *response = calloc(1, 4096);
    char *payload = calloc(1, 4096);
    char *id;
    char *id_end;

    snprintf(payload, 4096,
    "{"
    "\"Image\":\"%s\","
    "\"Cmd\":[\"%s\"],"
    "\"HostConfig\":{"
        "\"Binds\":[\"/tmp/.X11-unix:/tmp/.X11-unix\"]"
    "}"
    "}",
    image, cmd);
    
    snprintf(request, sizeof(request),
        "POST /containers/create?name=%s_vm HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        username, strlen(payload), payload);


    write(sock, request, strlen(request));
    free(payload);

    read(sock, response, 4096);


    id = strstr(response, "\"Id\":\"");
    if(id == NULL){
        printf("[ E ] The Docker creation failed. This may be because the mashine exists already with that name!");
        return 1;
    }
    fflush(stdout);
    id += 6;
    id_end = strstr(id, "\"");
    *id_end = '\0';
    strncpy(*container_id, id, 64);

    close(sock);
    return 0;
}

int docker_start(const char *username){
    int sock = docker_connect();
    char request[1024] = {0};
    char response[4096];

    snprintf(request, 1024, 
        "POST /containers/%s_vm/start HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n\r\n", username);
    
    write(sock, request, strlen(request));

    read(sock, response, 4096);
    printf("[ D ]");
    printf(response);
    printf("\n");

    close(sock);

    return 0;
}

int docker_stop(const char *username){
    int sock = docker_connect();
    char request[1024] = {0};

    snprintf(request, 1024, 
        "POST /containers/%s_vm/stop HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n\r\n", username);
    
    write(sock, request, strlen(request));
    close(sock);

    return 0;
}

int docker_delete(const char *username){
    int sock = docker_connect();
    char request[1024] = {0};
    snprintf(request, sizeof(request),
        "DELETE /containers/%s_vm?force=true HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n\r\n", username);
    write(sock, request, strlen(request));
    close(sock);
    return 0;
}