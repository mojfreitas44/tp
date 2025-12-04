#include "comum.h"

char pipe_cliente_nome[100];
int fd_cliente_pipe = -1;
int km_percorridos_final = 0;

// ============================================================================
// GESTÃO DE RECURSOS E SINAIS
// ============================================================================

void limpar_recursos() {
    if (fd_cliente_pipe != -1) close(fd_cliente_pipe);
    // Não é preciso unlink porque o veículo já não cria pipe próprio
}

void trata_sinal_cancelar(int s) {
    // Requisito: "Caso receba o sinal SIGUSR1 deve cancelar o serviço"
    printf("[RELATORIO] %d\n", km_percorridos_final);
    printf("Viagem cancelada pelo controlador!\n"); 
    fflush(stdout); // Garante que o controlador lê isto imediatamente
    
    // Avisa o cliente se possível
    if (fd_cliente_pipe != -1) {
        Mensagem m;
        m.pid = getpid();
        strcpy(m.comando, "fim");
        strcpy(m.mensagem, "Viagem cancelada pela central!");
        write(fd_cliente_pipe, &m, sizeof(Mensagem));
    }
    exit(0); // Chama o atexit(limpar_recursos)
}

void setup_ambiente(int pid_cliente) {
    setbuf(stdout, NULL); // Desativa buffer para o controlador ler logo
    atexit(limpar_recursos);
    
    // Configura sinais para cancelamento
    signal(SIGUSR1, trata_sinal_cancelar);
    signal(SIGINT, trata_sinal_cancelar);

    // Define o nome do pipe do cliente para enviar notificações
    sprintf(pipe_cliente_nome, PIPE_CLIENTE, pid_cliente);
}

// ============================================================================
// FASES DO SERVIÇO
// ============================================================================

void iniciar_viagem(const char *local) {
    // 1. Contactar Cliente
    fd_cliente_pipe = open(pipe_cliente_nome, O_WRONLY);
    if (fd_cliente_pipe == -1) {
        printf("Erro: Cliente incontactável. Abortar.\n"); 
        exit(1);
    }

    Mensagem msg;
    msg.pid = getpid();
    strcpy(msg.comando, "status");
    
    // Informa que chegou e começa logo (Simplificação do Prof)
    sprintf(msg.mensagem, "Veículo chegou a %s. A iniciar viagem...", local);
    write(fd_cliente_pipe, &msg, sizeof(Mensagem));


}

void realizar_viagem_simulada(int distancia_total) {
    //printf("Início da viagem de %dkm.\n", distancia_total); // Para o Controlador

    int perc = 0;
    
    
    // Loop simples de simulação
    while (km_percorridos_final < distancia_total) {
        sleep(1); // Avanço do tempo (1s = 1 unidade de tempo)
        km_percorridos_final++;
        
        int nova_perc = (km_percorridos_final * 100) / distancia_total;
        
        // Reporta a cada 10% ao Controlador (via stdout)
        if (nova_perc / 10 > perc / 10) {
            printf("Progresso: %d%% (%d/%d km)\n", nova_perc, km_percorridos_final, distancia_total);
            fflush(stdout); // Importante para o pipe anónimo não ficar preso
        }
        perc = nova_perc;
    }

    // Reporta o total final ao Controlador
    printf("[RELATORIO] %d\n", km_percorridos_final);
    printf("Viagem concluída com sucesso.\n");
    
    // Avisa o Cliente
    Mensagem msg_fim;
    msg_fim.pid = getpid();
    strcpy(msg_fim.comando, "fim");
    strcpy(msg_fim.mensagem, "Chegámos ao destino.");
    write(fd_cliente_pipe, &msg_fim, sizeof(Mensagem));
}

int main(int argc, char *argv[]) {
    // Validação para impedir execução manual
    if (argc != 5) {
        printf("[ERRO] Este programa é iniciado automaticamente pelo Controlador.\n");
        return 1;
    }

    // Parsing dos argumentos recebidos do Controlador
    // argv[1]=user, argv[2]=pid_cli, argv[3]=dist, argv[4]=local
    int pid_cliente = atoi(argv[2]);
    int distancia = atoi(argv[3]);
    char *local = argv[4];

    // 1. Configuração
    setup_ambiente(pid_cliente);

    // 2. Avisar chegada e início automático
    iniciar_viagem(local);

    // 3. Simular o percurso
    realizar_viagem_simulada(distancia);

    return 0;
}