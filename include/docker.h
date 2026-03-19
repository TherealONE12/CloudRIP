#ifndef DOCKER_H
#define DOCKER_H

// Container creation + ID returning
// returns 0 at sucsess, -1 at error
// container_id needs to be at least 65 chars big!!!
int docker_create(const char *image,const char *username, const char *cmd, char **container_id);

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
    int mousedown;
    int mousebuttonid;
    char key[2];
    char keycoode[8];
    int keyup;
    int dx;
    int dy;
} decodedCallback_t;

//Jsons Phraser (unsafe but working)
decodedCallback_t docker_helper_ConvertInputData(char inputdata[]);

//Cool injektor (probably slow AF)
int docker_injekt_Keystroke(char *inputData, int *needskeyup, decodedCallback_t *keyUpSafe);

#endif