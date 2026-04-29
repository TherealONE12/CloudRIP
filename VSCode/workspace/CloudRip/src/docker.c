#include "../include/docker.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h> 
#include <stdio.h>
#include <pthread.h>
#include <ctype.h>

int mouse_dirty = 1;

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


int docker_create(const char *image, const char *username, const char *cmd, char **container_id, int display_num){
    int sock = docker_connect();
    char request[4096];
    char *response = calloc(1, 4096);
    char *payload = calloc(1, 8192);
    char *id;
    char *id_end;
    
    // Wrapper script:
    // 1. Start Xvfb on :0 with access control disabled
    // 2. Start ffmpeg in background, pipe output to nc listening on localhost:9999
    // 3. Start game
    // This allows host to connect to container:9999 and read ffmpeg IVF stream
    char wrapped_cmd[2048];
    snprintf(wrapped_cmd, sizeof(wrapped_cmd),
        "Xvfb :0 -screen 0 1280x720x24 -ac & "
        "sleep 1 && "
        "ffmpeg -loglevel warning -framerate 30 -video_size 1280x720 "
        "-f x11grab -i :0 -c:v libvpx -b:v 800k -g 30 -deadline realtime -cpu-used 4 "
        "-lag-in-frames 0 -fflags +flush_packets -f ivf pipe:1 2>/dev/null | "
        "nc -l -p 9999 & "
        "sleep 1 && "
        "%s",
        cmd);

    snprintf(payload, 8192,
    "{"
    "\"Image\":\"%s\","
    "\"Cmd\":[\"/bin/sh\",\"-c\",\"%s\"],"
    "\"Env\":[\"DISPLAY=:0\"],"
    "\"HostConfig\":{"
        "\"Binds\":[\"/tmp/.X11-unix:/tmp/.X11-unix\"],"
        "\"PortBindings\":{\"9999/tcp\":[{\"HostPort\":\"0\"}]}"
    "}"
    "}",
    image, wrapped_cmd);
    
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
        printf("[ E ] The Docker creation failed. This may be because the machine exists already with that name!");
        return 1;
    }
    fflush(stdout);
    id += 6;
    id_end = strstr(id, "\"");
    *id_end = '\0';
    strncpy(*container_id, id, 64);

    close(sock);
    free(response);
    return 0;
}

int docker_start(const char *username){
    int sock = docker_connect();
    if (sock < 0) return -1;
    char request[1024] = {0};
    char response[4096] = {0};

    snprintf(request, sizeof(request),
        "POST /containers/%s_vm/start HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n\r\n", username);

    write(sock, request, strlen(request));
    read(sock, response, sizeof(response) - 1);
    close(sock);

    if (strstr(response, "204") || strstr(response, "304")) {
        printf("[ I ] Container %s_vm started\n", username);
    } else {
        printf("[ E ] Container start failed: %.120s\n", strstr(response, "\r\n\r\n") ? strstr(response, "\r\n\r\n") + 4 : response);
    }
    return 0;
}

int docker_stop(const char *username){
    int sock = docker_connect();
    if (sock < 0) return -1;
    char request[1024] = {0};
    char response[4096] = {0};

    snprintf(request, sizeof(request),
        "POST /containers/%s_vm/stop?t=2 HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n\r\n", username);

    write(sock, request, strlen(request));
    read(sock, response, sizeof(response) - 1);
    close(sock);
    return 0;
}

int docker_delete(const char *username){
    int sock = docker_connect();
    if (sock < 0) return -1;
    char request[1024] = {0};
    char response[4096] = {0};

    snprintf(request, sizeof(request),
        "DELETE /containers/%s_vm?force=true HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n\r\n", username);

    write(sock, request, strlen(request));
    read(sock, response, sizeof(response) - 1);
    close(sock);

    usleep(300000);
    return 0;
}



enum {
    INPUT_MOUSEMOVE = 0,
    INPUT_MOUSEDOWN = 1,
    INPUT_KEYDOWN = 2,
    INPUT_WHEEL = 3,
    INPUT_KEYUP = 4,
    INPUT_MOUSEUP = 5
};

static int json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    char needle[64];
    const char *start;
    size_t i = 0;

    if (!json || !key || !out || out_size < 2) {
        return 0;
    }

    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    start = strstr(json, needle);
    if (!start) {
        return 0;
    }

    start += strlen(needle);
    while (*start && i < out_size - 1) {
        if (*start == '\\') {
            start++;
            if (*start == 'n') out[i++] = '\n';
            else if (*start == 'r') out[i++] = '\r';
            else if (*start == 't') out[i++] = '\t';
            else if (*start == '"' || *start == '\\' || *start == '/') out[i++] = *start;
            else if (*start == '\0') break;
            start++;
            continue;
        }
        if (*start == '"') break;
        out[i++] = *start++;
    }
    out[i] = '\0';
    return i > 0;
}

static int json_get_int(const char *json, const char *key, int *out) {
    char needle[64];
    const char *start;

    if (!json || !key || !out) {
        return 0;
    }

    snprintf(needle, sizeof(needle), "\"%s\":", key);
    start = strstr(json, needle);
    if (!start) {
        return 0;
    }
    start += strlen(needle);
    *out = atoi(start);
    return 1;
}

static int is_safe_key_token(const char *key) {
    size_t i;
    if (!key || key[0] == '\0') return 0;
    for (i = 0; key[i] != '\0'; i++) {
        unsigned char c = (unsigned char)key[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '+')) {
            return 0;
        }
    }
    return 1;
}

decodedCallback_t docker_helper_ConvertInputData(char inputdata[]){
    decodedCallback_t ev = {0};
    char type[32] = {0};

    ev.mode = -1;
    if (!json_get_string(inputdata, "type", type, sizeof(type))) {
        return ev;
    }

    if (strcmp(type, "mousemove") == 0) {
        ev.mode = INPUT_MOUSEMOVE;
        json_get_int(inputdata, "x", &ev.x);
        json_get_int(inputdata, "y", &ev.y);
    } else if (strcmp(type, "mousedown") == 0) {
        ev.mode = INPUT_MOUSEDOWN;
        json_get_int(inputdata, "x", &ev.x);
        json_get_int(inputdata, "y", &ev.y);
        json_get_int(inputdata, "button", &ev.mousebuttonid);
    } else if (strcmp(type, "mouseup") == 0) {
        ev.mode = INPUT_MOUSEUP;
        json_get_int(inputdata, "x", &ev.x);
        json_get_int(inputdata, "y", &ev.y);
        json_get_int(inputdata, "button", &ev.mousebuttonid);
    } else if (strcmp(type, "keydown") == 0) {
        ev.mode = INPUT_KEYDOWN;
        json_get_string(inputdata, "key", ev.keydown, sizeof(ev.keydown));
        json_get_string(inputdata, "code", ev.keycoodedown, sizeof(ev.keycoodedown));
    } else if (strcmp(type, "keyup") == 0) {
        ev.mode = INPUT_KEYUP;
        json_get_string(inputdata, "key", ev.keyup, sizeof(ev.keyup));
        json_get_string(inputdata, "code", ev.keycoodeup, sizeof(ev.keycoodeup));
    } else if (strcmp(type, "wheel") == 0) {
        ev.mode = INPUT_WHEEL;
        json_get_int(inputdata, "dx", &ev.dx);
        json_get_int(inputdata, "dy", &ev.dy);
    }

    if (ev.x < 0) ev.x = 0;
    if (ev.y < 0) ev.y = 0;
    return ev;
}

int docker_injekt_Keystroke(char *inputData, int display_num){
    decodedCallback_t ev = docker_helper_ConvertInputData(inputData);
    int ret;
    char command[256];

    if (ev.mode < 0) {
        return 1;
    }

    switch(ev.mode){
        case INPUT_MOUSEMOVE:
            snprintf(command, sizeof(command), "DISPLAY=:%d xdotool mousemove %d %d", display_num, ev.x, ev.y);
            ret = system(command) / 256;
            if (ret != 0) printf("[ E ] Command failed (%d): %s\n", ret, command);
            break;

        case INPUT_MOUSEDOWN:
        case INPUT_MOUSEUP: {
            int button = ev.mousebuttonid + 1;
            if (button < 1 || button > 5) button = 1;

            snprintf(command, sizeof(command), "DISPLAY=:%d xdotool mousemove %d %d", display_num, ev.x, ev.y);
            ret = system(command) / 256;
            if (ret != 0) printf("[ E ] Command failed (%d): %s\n", ret, command);

            snprintf(command, sizeof(command), "DISPLAY=:%d xdotool %s %d", display_num,
                     ev.mode == INPUT_MOUSEDOWN ? "mousedown" : "mouseup", button);
            ret = system(command) / 256;
            if (ret != 0) printf("[ E ] Command failed (%d): %s\n", ret, command);
            break;
        }

        case INPUT_KEYDOWN:
        case INPUT_KEYUP: {
            const char *key = (ev.mode == INPUT_KEYDOWN) ? to_xdotool_key(ev.keydown) : to_xdotool_key(ev.keyup);
            if (!is_safe_key_token(key)) {
                return 1;
            }
            snprintf(command, sizeof(command), "DISPLAY=:%d xdotool key%s %s", display_num,
                     ev.mode == INPUT_KEYDOWN ? "down" : "up", key);
            ret = system(command) / 256;
            if (ret != 0) printf("[ E ] Command failed (%d): %s\n", ret, command);
            break;
        }

        case INPUT_WHEEL: {
            int steps = abs(ev.dy) / 60;
            int wheel_button = ev.dy > 0 ? 5 : 4;
            int i;
            if (steps < 1) steps = 1;
            if (steps > 12) steps = 12;
            for (i = 0; i < steps; i++) {
                snprintf(command, sizeof(command), "DISPLAY=:%d xdotool click %d", display_num, wheel_button);
                ret = system(command) / 256;
                if (ret != 0) printf("[ E ] Command failed (%d): %s\n", ret, command);
            }
            break;
        }
    }

    return 0;
}

const char* to_xdotool_key(const char* key) {
    static const char* map[][2] = {
        {"Enter",      "Return"},
        {"Backspace",  "BackSpace"},
        {" ",          "space"},
        {"ArrowUp",    "Up"},
        {"ArrowDown",  "Down"},
        {"ArrowLeft",  "Left"},
        {"ArrowRight", "Right"},
        {"Escape",     "Escape"},
        {"Tab",        "Tab"},
        {"Delete",     "Delete"},
        {"Control",    "ctrl"},
        {"Shift",      "shift"},
        {"Alt",        "alt"},
        {"CapsLock",   "Caps_Lock"},
        {"Meta",       "Super_L"},
        {NULL, NULL}
    };

    for (int i = 0; map[i][0] != NULL; i++) {
        if (strcmp(key, map[i][0]) == 0)
            return map[i][1];
    }
    return key;
}
