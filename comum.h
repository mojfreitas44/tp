#ifndef COMUM_H
#define COMUM_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#define PIPE_CONTROLADOR "controlador_fifo"
#define PIPE_CLIENTE "pipe%d"
#define NVEICULOS 10
#define NUTILIZADORES 2
#define MAX_AGENDAMENTOS 50

typedef struct {
    pid_t pid;             // PID do cliente
    char comando[50];      // tipo de comando
    char username[50];     // nome do utilizador
    char mensagem[256];    // mensagem adicional
} Mensagem;

#endif 