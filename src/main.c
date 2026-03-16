//#include "docker.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h> 
#include <stdio.h>

typedef struct decodedCallback{
    int mode;
    int x;
    int y;
    int mousedown;
    int mousebuttonid;
} decodedCallback_t;


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

int docker_injekt_Keystroke(char *inputData){





    return 0;
}


decodedCallback_t docker_helper_ConvertInputData(char inputdata[]){
    int mode;
    char x[8] = {0};
    char y[8] = {0};
    char button[8] = {0};
    char *result;
    decodedCallback_t returnvalue = {0};

    if(strstr(inputdata, "mousemove") != NULL){
        mode = 0;
    }else if(strstr(inputdata,"mousedown")!= NULL){
        mode = 1;

    }else if(strstr(inputdata, "keydown")!= NULL){
        mode = 2;

    }else if(strstr(inputdata, "wheel")!= NULL){
        mode = 3;

    }



/*   Possible Input data formats

{ "type": "mousemove", "x": 320, "y": 240 }
{ "type": "mousedown", "button": 0, "x": 320, "y": 240 }
{ "type": "keydown",   "key": "a", "code": "KeyA" }
{ "type": "wheel",     "dx": 0,    "dy": 120 }*/ 
    switch(mode) {
        case 0:
                        
            fflush(stdout);
            inputdata +=25; //skip 24 Letters to the right
            result = strchr(inputdata, ',');

             if(result != NULL){
                for(int i = 0; i<strlen(inputdata) - strlen(result); i++){
                    x[i] = inputdata[i];
                }

                printf("[ I ] y=%s\n", x);
                
            }else{
                printf("[ E ] WHAT... \n");
            }

            inputdata += strlen(inputdata) - strlen(result) +5; //skip 24 Letters to the right
            result = strchr(inputdata, '}');

            if(result != NULL){
                for(int i = 0; i<strlen(inputdata) - strlen(result); i++){
                    y[i] = inputdata[i]; 
                    
                }

                printf("[ I ] y=%s\n", y);
                
            }else{
                printf("[ E ] WHAT... \n");
            }
            fflush(stdout);
            returnvalue.x = atoi(x);
            returnvalue.y = atoi(y);
            returnvalue.mode = 0;

            break;
        case 1:

            inputdata +=29; //skip 29 Letters to the right
            result = strchr(inputdata, ',');

            if(result != NULL){
                for(int i = 0; i<strlen(inputdata) - strlen(result); i++){
                    button[i] = inputdata[i];
                }

                printf("[ I ] Button_ID=%s\n", button);
                
            }else{
                printf("[ E ] WHAT... \n");
            }

            inputdata += strlen(inputdata) - strlen(result) +5; //skip 5 Letters to the right
            result = strchr(inputdata, ',');

            if(result != NULL){
                for(int i = 0; i<strlen(inputdata) - strlen(result); i++){
                    x[i] = inputdata[i];
                }

                printf("[ I ] x=%s\n", x);
                
            }else{
                printf("[ E ] WHAT... \n");
            }


                        inputdata += strlen(inputdata) - strlen(result) +5; //skip 5 Letters to the right
            result = strchr(inputdata, '}');

            if(result != NULL){
                for(int i = 0; i<strlen(inputdata) - strlen(result); i++){
                    y[i] = inputdata[i];
                }

                printf("[ I ] y=%s\n", y);
                
            }else{
                printf("[ E ] WHAT... \n");
            }


            fflush(stdout);
            returnvalue.x = atoi(x);
            returnvalue.y = atoi(y);
            returnvalue.mousedown = 1;
            returnvalue.mousebuttonid = atoi(button);
            returnvalue.mode = 1;

            break;

    }

    return returnvalue;

}

int main(){
    decodedCallback_t value;

    value = docker_helper_ConvertInputData("{\"type\":\"mousedown\",\"button\":2,\"x\":67,\"y\":420}");


    printf("x=%i, y=%i, Mousedown=%i, MouseButtonID=%i\n", value.x, value.y, value.mousedown, value.mousebuttonid);
    return 0;
}