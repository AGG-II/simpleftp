#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 512
#define MSGSIZE 1000

//Operations:
#define RETRIEVE "RETR"
#define QUIT "QUIT"

// Codes:
#define HELLOMSG 220
#define USREXISTS 331
#define PASSOK 230
#define FILEXISTS 299
#define TRNSFRCMPL 226
#define GDBY 221 

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer is received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer

    recv_s = recv(sd, buffer, BUFSIZE, 0);

    // error checking
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    // If you want to see exactly what messages the server is receiving then uncomment the line below
    //printf("%d %s\n", recv_code, message); 
    // optional copy of parameters
    if(text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";
    int send_s;

    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors

    send_s = send(sd, buffer,sizeof(buffer),0);
    if (send_s < 0) warn("error sending data");

}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input, desc[BUFSIZE];

    // ask for user
    printf("username: ");
    input = read_input();

    // send the command to the server
    
    send_msg(sd, "USER", input);

    // release memory
    free(input);

    // wait to receive password requirement and check for errors

    if(!recv_msg(sd, USREXISTS, desc)){
        errx(1, "error while trying to authenticate");
    }
    printf("%s\n", desc);

    // ask for password
    printf("passwd: ");
    input = read_input();

    // send the command to the server

    send_msg(sd, "PASS", input);

    // release memory
    free(input);
    
    // wait for answer and process it and check for errors
    bool res = recv_msg(sd, PASSOK, desc);
    printf("%s\n", desc);
    if(!res){
        errx(1, "error while trying to send password");
    }

}

/**
 * function: Opens the data stream socket and sends the PORT command
 * sd: socket descriptor
 * return: returns the socket descriptor of the data stream
 */
int set_data(int sd){

    int dd;
    struct sockaddr_in dataPort;
    socklen_t dataLen = sizeof(dataPort);
    char portArgs[BUFSIZE];

    dd = socket(AF_INET, SOCK_STREAM, 0);
    if(dd < 0 ){
        errx(1, "Error while trying to open data socket");
    }

    dataPort.sin_family = AF_INET;
    dataPort.sin_addr.s_addr = htonl(INADDR_ANY);
    dataPort.sin_port = htons(0); 
    if (bind(dd, (struct sockaddr*)&dataPort, sizeof(dataPort)) < 0) {
        errx(1, "Error while trying to bind in data");
    }

    // We retrieve the info from the socket
    if (getsockname(dd, (struct sockaddr *)&dataPort, &dataLen) < 0) {
        close(dd);
        errx(1, "Error while trying to get data info");
    }

    uint32_t ip_addr = ntohl(dataPort.sin_addr.s_addr);
    unsigned char ip[4];
    ip[0] = (ip_addr >> 24) & 0xFF;
    ip[1] = (ip_addr >> 16) & 0xFF;
    ip[2] = (ip_addr >> 8) & 0xFF;
    ip[3] = ip_addr & 0xFF;
    uint16_t portNum = ntohs(dataPort.sin_port);

    sprintf(portArgs, "%d,%d,%d,%d,%d,%d", ip[0], ip[1], ip[2], ip[3], portNum/256, portNum %256);

    if(listen(dd, 1) < 0){
        close(dd);
        errx(1, "ERROR while trying to listen.\n");
    }

    send_msg(sd,"PORT", portArgs);

    return dd;

}

void recv_file(int serverdd, FILE* file, long int fSize){
    char buffer[BUFSIZE];
    int bufLen;
    long int received = 0;
    for(; (bufLen = recv(serverdd, buffer, BUFSIZE, 0)) && received <= fSize; received += bufLen){
        if(bufLen == -1){
            warn("Error while trying to retrieve file");
            break;
        }
        fwrite(buffer, sizeof(char), bufLen, file);
    }
    close(serverdd);
}

/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE], text[MSGSIZE];
    long int f_size;
    FILE *file;

    // Open the data stream and send PORT command
    int dd = set_data(sd);

    // send the RETR command to the server

    send_msg(sd, RETRIEVE, file_name);

    // check for the response

    if(!recv_msg(sd, FILEXISTS, buffer)){
        warn(buffer);
        return;
    }
    printf("%s\n",buffer);

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %ld bytes", &f_size);

    // open the file to write
    file = fopen(file_name, "w");

    //receive the file
    struct sockaddr_in serverData;
    socklen_t serverLen = sizeof(serverData);
    int serverdd = accept(dd, (struct sockaddr *) &serverData, &serverLen);
    
    recv_file(serverdd, file, f_size);

    // close the file
    fclose(file);

    // receive the OK from the server
    if(!recv_msg(sd, TRNSFRCMPL, text))
        warn(text);
    else
        printf("%s\n", text);

}


/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {
    char text[BUFSIZE];
    // send command QUIT to the server
    send_msg(sd, QUIT, NULL);
    // receive the answer from the server
    if(recv_msg(sd, GDBY, text))
        perror(text);
    else
        printf("%s\n", text);
}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        }
        else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        }
        else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int sd;
    struct sockaddr_in addr;
    char hello_msg[BUFSIZE];

    // arguments checking

    if (argc < 3) {
        errx(1, "Port expected as argument");
    } else if (argc > 3) {
        errx(1, "Too many arguments");
    }

    // create socket and check for errors

    sd = socket(PF_INET, SOCK_STREAM, 0);
    // Si el socket no se pudo crear
    if(sd == -1) {
        perror("Socket error\n");
        return -1;
    }

    // set socket data    


    addr.sin_family = AF_INET;
    if(inet_pton(AF_INET, argv[1], &(addr.sin_addr)) <= 0) return -1;
    addr.sin_port = htons(atoi(argv[2]));
    memset(&(addr.sin_zero), 0, 8);

    // connect and check for errors


    if(connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ){
        perror("Conection failed");
        close(sd);
        return -1;
    }


    // if receive hello proceed with authenticate and operate if not warning
    if(recv_msg(sd, HELLOMSG, hello_msg)){
        printf("%s\n", hello_msg);
    } else {
        close(sd);
        errx(1, "Connection unsuccessful\n");
    }
    authenticate(sd);
    operate(sd);
    
    // close socket
    close(sd);
    return 0;
}
