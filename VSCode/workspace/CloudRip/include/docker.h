#ifndef DOCKER_H
#define DOCKER_H

// Container creation + ID returning
// returns 0 at sucsess, -1 at error
// container_id needs to be at least 65 chars big!!!
int docker_create(const char *image, const char *username, const char *cmd, char **container_id, int display_num);

// Container starting
int docker_start(const char *username);

// Container stopping
int docker_stop(const char *username);

// Container detelting
int docker_delete(const char *username);

//JSONS pharsing stuff
typedef struct decodedCallback{
    int mode;
    int x;
    int y;
    int mousebuttonid;
    char keyup[64];
    char keycoodeup[32];
    char keydown[64];
    char keycoodedown[32];
    int dx;
    int dy;
} decodedCallback_t;

//Json parser for input callbacks
decodedCallback_t docker_helper_ConvertInputData(char inputdata[]);

//Cool injektor (probably slow AF)
int docker_injekt_Keystroke(char *inputData, int display_num);

//Make it readable for XDOTOOL the input data
const char* to_xdotool_key(const char* key);

//mouse on extra thread so it doenst block stuff
void* mouse_thread(void* arg);



#endif
