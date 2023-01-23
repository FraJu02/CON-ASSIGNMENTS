#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "framework.h"
#define BONUS_TASK
#define RTSP_VERSION ("RTSP/1.0")


// Handle a server-client connection. This method is called 
// for each separate connection
void handleConnection(int fd, const char *remote_addr, uint16_t remote_port)
{
    read_info info;
    MessageType currentMessage = INVALID;
    AVContext *context = nullptr;
    // TODO: find a way to generate session ids
    int session_id;
    session_id = rand();
    int client_port;
    // Initially, the state is set to initialization
    // Refer to the assignment description for the state logic
    ConnectionState currentState = INITIALIZATION;
    int cseq;
    while(true)
    {
        // Setup the read operation and read
        // all lines from the socket
        printf("------------------\n");
        setup_read(&info, fd);
        read_all_lines(&info);
        printf("Read %ld lines\n", info.numlines);
        // Parse the method and the path from the first line
        char* method = info.lines[0];
        if (!method)
            goto empty_read;
        char* methodEnd = strchr(method, ' ');
        if (!methodEnd)
            goto bad_request;
        char* pathEnd = strrchr(methodEnd+1, ' ');
        if (!pathEnd)
            goto bad_request;
        (*methodEnd) = '\0';
        (*pathEnd) = '\0';
        char *path = methodEnd+1;
        // Extract information from the read lines
        currentMessage = stringToMessageType(method);
        printf("The current message type is %s\n", messageTypeToString(currentMessage));
        printf("Path is %s\n",path);
        // Get the CSeq value from the received message
        char *CSeq = search_for_header(&info, "CSeq:");
        if (!CSeq)
            goto missing_cseq;
        CSeq = strchr(CSeq, ' ');
        if (!CSeq)
            goto missing_cseq;
        cseq = atoi(CSeq);
        printf("CSeq is %d\n",cseq);
        if(currentState == INITIALIZATION)
            currentState = INITIALIZATION;
        
        switch(currentMessage)
        {
            case OPTIONS:
            {
                char const  supported_methods[] = "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN";
                dprintf(fd,"RTSP/1.0 200 OK\r\nCSeq: %d\r\nPublic: %s\r\n\r\n",
                cseq, supported_methods);
                break;
            }
            case DESCRIBE:
            {
                char p[200];
                if(!fileExists(filename_from_path(path)))
                {
                    dprintf(fd, "RTSP/1.0 404 Not Found");
                    break;
                }
                int errorcode = getSDPInfo(filename_from_path(path), p,200);
                int len = strlen(p);
                if(errorcode == -1)
                {
                    dprintf(fd, "RTSP/1.0 500 Internal Server Error");
                    break;
                }
                else
                {
                    dprintf(fd,"RTSP/1.0 200 OK\r\nCSeq: %d\r\nContent-Type: application/sdp\r\nContent-Length: %i\r\n\r\n%s\r\n\r\n",
                    cseq, len, p);
                }
                break;
            } 
            case SETUP:
            {
                char* ports = search_for_header(&info,"Transport");

                if(ports == nullptr || strlen(ports) == 0)
                {
                    free(ports);
                    dprintf(fd, "RTSP/1.0 500 Internal Server Error");
                    break;
                }
                //dprintf(fd, "%s\r\n", ports);
                //int media_port = 0;
                //int control_port = 0;
                char current_port[5];
                //char current_port2[5];
                for(int i = 0; i < strlen(ports); i++)
                {
                    if(ports[i] == '=')
                    {
                        current_port[0] = ports[i + 1];
                        current_port[1] = ports[i + 2];
                        current_port[2] = ports[i + 3];
                        current_port[3] = ports[i + 4];
                        //media_port = atoi(current_port);
                        client_port = atoi(current_port);
                        //current_port2[0] = ports[i + 6];
                        //current_port2[1] = ports[i + 7];
                        //current_port2[2] = ports[i + 8];
                        //current_port2[3] = ports[i + 9];
                        //control_port = atoi(current_port2);
                        break;

                    }
                }
                
                dprintf(fd, "RTSP/1.0 200 OK\r\nCSeq: %i\r\n%s\r\nSession: %i\r\n\r\n", cseq,  ports, session_id);
                context = createAVContext(filename_from_path(path), client_port);
                currentState = READY;
                free(ports);
                break;
            }
            case PLAY:
            {
// TODO: Implement this method!
                //dprintf(fd, "RTSP/1.0 200 OK\r\n");
                dprintf(fd, "RTSP/1.0 200 OK\r\nCSeq: %i\r\nSession: %i\r\n\r\n", cseq, session_id);


                AVPacket packy = {0};
                currentState = PLAYING;
                while(readPacketFromContext(context, &packy) == 0)
                {
                    packy.duration = av_rescale(packy.duration, context->inputStream->time_base.num * 1000000, context->inputStream->time_base.den);
                    rescalePacketTimestamps(context, &packy);
                    usleep(packy.duration);  
                    sendAndFreePacket(context, &packy);
                }
                deleteAVContext(context);                         
                currentState = INITIALIZATION;
                break;
            } 
            case PAUSE:
            {
                dprintf(fd, "RTSP/1.0 501 Not Implemented");
                break;
            }
            case TEARDOWN:
            {
                session_id = -1;
                deleteAVContext(context);
                currentState = INITIALIZATION; 
                break;
            }
            default:
            {
                printf("Unknown message type\n");
                dprintf(fd, "RTSP/1.0 501 Not Implemented");
            }
        }
        complete_read(&info);
    }
    printf("Done receiving\n");
    goto close;
    empty_read:
        printf("Client disconnected\n");
        goto close;
    bad_request:
        printf("Invalid request\n");
        goto close;
    missing_cseq:
        printf("Missing CSeq header\n");
        goto close;
    close:
        complete_read(&info);
        close(fd);
        deleteAVContext(context);
        context = nullptr;
        return;
}
