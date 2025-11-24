#include "comum.h"

// Estrutura do Veículo (Frota)
typedef struct {
    pid_t pid;
    int fd_leitura;     
    int ocupado;
    pid_t pid_cliente;  
    char ultimo_status[50]; 
    int distancia_viagem;
    int id_servico; 
} Veiculo;

// Estrutura de Agendamento (Lista de Espera)
typedef struct {
    int id; 
    char username[50];
    pid_t pid_cliente;
    int hora;
    int distancia;
    char local[100];
    int ativo; // 1 = Pendente, 0 = Vazio
} Agendamento;

// Estrutura de Informação do Cliente
typedef struct {
    pid_t pid;
    char username[50];
} ClienteInfo;

// Estrutura Geral do Controlador
typedef struct {
    Veiculo frota[NVEICULOS];
    ClienteInfo clientes[NUTILIZADORES];
    Agendamento agenda[MAX_AGENDAMENTOS];
    int num_veiculos;   
    int fd_clientes;    
    int tempo;          
    int total_km;       
    int proximo_id; 
} Controlador;

static Controlador ctrl;

// ============================================================================
// FUNÇÕES AUXILIARES GERAIS
// ============================================================================

void log_msg(const char *tag, const char *msg) {
    printf("[TEMPO %03d] %-12s %s\n", ctrl.tempo, tag, msg);
    fflush(stdout);
}

// CORREÇÃO: Agora retorna int (1=Sucesso, 0=Cheio)
int registar_cliente(pid_t pid, char* nome) {
    for (int i = 0; i < NUTILIZADORES; i++) {
        if (ctrl.clientes[i].pid == 0) { // Encontrou slot vazio
            ctrl.clientes[i].pid = pid;
            strcpy(ctrl.clientes[i].username, nome);
            return 1; // Sucesso
        }
    }
    return 0; // Lista cheia
}

void remover_cliente(pid_t pid) {
    for (int i = 0; i < NUTILIZADORES; i++) {
        if (ctrl.clientes[i].pid == pid) {
            ctrl.clientes[i].pid = 0;
            ctrl.clientes[i].username[0] = '\0';
            break;
        }
    }
    // Cancelar agendamentos pendentes deste cliente
    int cancelados = 0;
    for (int i=0; i<MAX_AGENDAMENTOS; i++){
        if(ctrl.agenda[i].ativo == 1 && ctrl.agenda[i].pid_cliente == pid){
            ctrl.agenda[i].ativo = 0;
            cancelados++;
        }
    }
    if(cancelados > 0){
        char msg[100];
        sprintf(msg, "Cancelados %d agendamentos pendentes do cliente (PID %d).", cancelados, pid);
        log_msg("[CANCELAR]", msg);
    }
}

void limpar_recursos() {
    printf("\n[SISTEMA] A encerrar controlador e notificar todos...\n");
    
    for (int i = 0; i < NUTILIZADORES; i++) {
        if (ctrl.clientes[i].pid > 0) {
            kill(ctrl.clientes[i].pid, SIGUSR1);
        }
    }

    for (int i = 0; i < ctrl.num_veiculos; i++) {
        if (ctrl.frota[i].pid > 0) {
            kill(ctrl.frota[i].pid, SIGKILL); 
            close(ctrl.frota[i].fd_leitura);
        }
    }
    
    if (ctrl.fd_clientes != -1) close(ctrl.fd_clientes);
    unlink(PIPE_CONTROLADOR);
}

void handler_sinal(int s) {
    exit(0); 
}

void setup_inicial() {
    setbuf(stdout, NULL); 
    memset(&ctrl, 0, sizeof(Controlador));
    ctrl.fd_clientes = -1;
    ctrl.proximo_id = 1; 

    int fd_check = open(PIPE_CONTROLADOR, O_WRONLY | O_NONBLOCK);
    if (fd_check != -1) {
        printf("[ERRO] Já existe uma instância do programa controlador em execução!\n");
        close(fd_check);
        exit(1); 
    }

    signal(SIGINT, handler_sinal);
    atexit(limpar_recursos);

    if (mkfifo(PIPE_CONTROLADOR, 0666) == -1 && errno != EEXIST) {
        perror("[ERRO] Falha no mkfifo");
        exit(1);
    }

    ctrl.fd_clientes = open(PIPE_CONTROLADOR, O_RDONLY | O_NONBLOCK);
    if (ctrl.fd_clientes == -1) {
        perror("[ERRO] Falha no open do FIFO");
        exit(1);
    }

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    printf("\n=== CONTROLADOR DE TÁXIS ===\n");
    printf("--- Comandos Admin ---\n");
    printf(" listar        -> Ver agendamentos\n");
    printf(" utiliz        -> Ver utilizadores ligados\n");
    printf(" frota         -> Ver estado dos veículos\n");
    printf(" km            -> Ver total de KMs\n");
    printf(" hora          -> Ver tempo simulado\n");
    printf(" cancelar <ID> -> Cancelar serviço (0 para todos)\n");
    printf(" terminar      -> Encerrar sistema\n");
    printf("----------------------------\n");

    log_msg("[SISTEMA]", "Controlador iniciado.");
}

void enviar_resposta(pid_t pid_cli, char *texto) {
    char p_name[100];
    sprintf(p_name, PIPE_CLIENTE, pid_cli);
    
    int fd = open(p_name, O_WRONLY | O_NONBLOCK);
    if (fd != -1) {
        Mensagem m;
        m.pid = getpid();
        strcpy(m.comando, "resposta");
        strncpy(m.mensagem, texto, 255);
        write(fd, &m, sizeof(Mensagem));
        close(fd);
    }
}

// ============================================================================
// GESTÃO DE AGENDAMENTOS E FROTA (IDs)
// ============================================================================

void registar_agendamento_na_lista(int id_servico, char* user, pid_t pid, int h, int d, char* loc) {
    for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
        if (ctrl.agenda[i].ativo == 0) {
            ctrl.agenda[i].id = id_servico; 
            strcpy(ctrl.agenda[i].username, user);
            ctrl.agenda[i].pid_cliente = pid;
            ctrl.agenda[i].hora = h;
            ctrl.agenda[i].distancia = d;
            strcpy(ctrl.agenda[i].local, loc);
            ctrl.agenda[i].ativo = 1; 
            
            char msg[100];
            sprintf(msg, "Agendado ID %d para t=%d (Slot %d)", id_servico, h, i);
            log_msg("[AGENDA]", msg);
            return;
        }
    }
    log_msg("[ERRO]", "Lista de agendamentos cheia!");
}

int cancelar_servico(pid_t pid_solicitante, int id_cancelar) {
    int cancelados = 0;

    // --- 1. Cancelar Veículos em Andamento (FROTA) ---
    for (int i = 0; i < ctrl.num_veiculos; i++) {
        if (ctrl.frota[i].pid > 0) {
            int alvo = 0;
            
            if (pid_solicitante == -1) { // ADMIN
                if (id_cancelar == 0) alvo = 1; 
                else if (ctrl.frota[i].id_servico == id_cancelar) alvo = 1;
            } else { // CLIENTE
                if (ctrl.frota[i].pid_cliente == pid_solicitante) {
                    if (id_cancelar == 0) alvo = 1; 
                    else if (ctrl.frota[i].id_servico == id_cancelar) alvo = 1; 
                }
            }

            if (alvo) {
                kill(ctrl.frota[i].pid, SIGUSR1); 
                strcpy(ctrl.frota[i].ultimo_status, "A cancelar..."); 
                
                cancelados++;
                printf("[SISTEMA] Sinal de cancelamento enviado ao Veículo %d (Serviço ID %d).\n", ctrl.frota[i].pid, ctrl.frota[i].id_servico);
            }
        }
    }

    // --- 2. Cancelar Agendamentos Pendentes (AGENDA) ---
    for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
        if (ctrl.agenda[i].ativo) {
            int alvo = 0;

            if (pid_solicitante == -1) { // ADMIN
                if (id_cancelar == 0) alvo = 1;
                else if (ctrl.agenda[i].id == id_cancelar) alvo = 1;
            } else { // CLIENTE
                if (ctrl.agenda[i].pid_cliente == pid_solicitante) {
                    if (id_cancelar == 0) alvo = 1;
                    else if (ctrl.agenda[i].id == id_cancelar) alvo = 1;
                }
            }

            if (alvo) {
                ctrl.agenda[i].ativo = 0; 
                cancelados++;
                
                if (pid_solicitante == -1) {
                    char aviso[100];
                    sprintf(aviso, "O teu agendamento (ID %d) foi cancelado pelo Admin.", ctrl.agenda[i].id);
                    enviar_resposta(ctrl.agenda[i].pid_cliente, aviso);
                }
                
                printf("[SISTEMA] Agendamento ID %d removido da lista.\n", ctrl.agenda[i].id);
            }
        }
    }
    return cancelados;
}

// ============================================================================
// GESTÃO DE VEÍCULOS
// ============================================================================

void lancar_veiculo(char* user, int pid_cli, int dist, char* local, int id_servico) {
    int p[2];
    pid_t pid;
    char str_pid[20], str_dist[20];
    char buffer[100];

    if (ctrl.num_veiculos >= NVEICULOS) {
        log_msg("[AVISO]", "Frota cheia. Erro interno (verificar_agendamentos falhou).");
        return;
    }

    if (pipe(p) == -1) {
        log_msg("[ERRO]", "Falha pipe anónimo");
        return;
    }

    pid = fork();

    if (pid == 0) { 
        // --- FILHO (VEÍCULO) ---
        close(p[0]); 
        close(STDOUT_FILENO); 
        dup(p[1]);            
        close(p[1]);          

        sprintf(str_pid, "%d", pid_cli);
        sprintf(str_dist, "%d", dist);

        execl("./veiculo", "veiculo", user, str_pid, str_dist, local, NULL);
        perror("[ERRO] execl falhou");
        _exit(1);

    } else if (pid > 0) { 
        // --- PAI (CONTROLADOR) ---
        close(p[1]); 
        int flags = fcntl(p[0], F_GETFL, 0);
        fcntl(p[0], F_SETFL, flags | O_NONBLOCK);

        int idx = ctrl.num_veiculos;
        ctrl.frota[idx].pid = pid;
        ctrl.frota[idx].pid_cliente = pid_cli; 
        ctrl.frota[idx].fd_leitura = p[0];
        ctrl.frota[idx].ocupado = 1;
        ctrl.frota[idx].distancia_viagem = dist;
        ctrl.frota[idx].id_servico = id_servico; 
        strcpy(ctrl.frota[idx].ultimo_status, "A iniciar");
        ctrl.num_veiculos++;

        sprintf(buffer, "Veículo enviado (Serviço ID %d) para %s", id_servico, user);
        log_msg("[FROTA]", buffer);
    }
}

void verificar_agendamentos() {
    for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
        if (ctrl.agenda[i].ativo == 1 && ctrl.agenda[i].hora <= ctrl.tempo) {
            
            if (ctrl.num_veiculos < NVEICULOS) {
                lancar_veiculo(ctrl.agenda[i].username, 
                             ctrl.agenda[i].pid_cliente, 
                             ctrl.agenda[i].distancia, 
                             ctrl.agenda[i].local,
                             ctrl.agenda[i].id); 
                
                ctrl.agenda[i].ativo = 0;
            }
        }
    }
}

void verificar_frota() {
    char buffer[256];
    char tag[20];
    int i, n;

    for (i = 0; i < ctrl.num_veiculos; i++) {
        if (ctrl.frota[i].pid > 0) { 
            n = read(ctrl.frota[i].fd_leitura, buffer, sizeof(buffer) - 1);
            
            if (n > 0) { 
                buffer[n] = '\0';
                if (buffer[strlen(buffer)-1] == '\n') buffer[strlen(buffer)-1] = '\0';
                
                strncpy(ctrl.frota[i].ultimo_status, buffer, 49);

                sprintf(tag, "[TAXI-%d|ID-%d]", ctrl.frota[i].pid, ctrl.frota[i].id_servico);
                printf("[TEMPO %03d] %-14s %s\n", ctrl.tempo, tag, buffer);
                fflush(stdout);

                if (strstr(buffer, "concluída") != NULL) {
                    ctrl.total_km += ctrl.frota[i].distancia_viagem;
                    strcpy(ctrl.frota[i].ultimo_status, "A terminar...");
                }
            }
            else if (n == 0) {
                close(ctrl.frota[i].fd_leitura);
                
                if (i != ctrl.num_veiculos - 1) {
                    ctrl.frota[i] = ctrl.frota[ctrl.num_veiculos - 1];
                    i--; 
                } else {
                    ctrl.frota[i].pid = 0;
                }
                
                ctrl.num_veiculos--;
            }
        }
    }
}

// ============================================================================
// GESTÃO DE PEDIDOS (CLIENTES)
// ============================================================================

void processar_comando_cliente(Mensagem *m) {
    char msg_buf[300];
    int h, d;
    char loc[100];

    if (strcmp(m->comando, "login") == 0) {
        int existe = 0;
        for (int i = 0; i < NUTILIZADORES; i++) {
            if (ctrl.clientes[i].pid > 0 && strcmp(ctrl.clientes[i].username, m->username) == 0) {
                existe = 1; break;
            }
        }

        char pipe_cli[100];
        sprintf(pipe_cli, PIPE_CLIENTE, m->pid);
        int fd = open(pipe_cli, O_WRONLY | O_NONBLOCK);

        if (existe) {
            if (fd != -1) {
                Mensagem erro;
                erro.pid = getpid();
                strcpy(erro.comando, "erro");
                sprintf(erro.mensagem, "Utilizador '%s' ja existe.", m->username);
                write(fd, &erro, sizeof(Mensagem));
                close(fd);
            }
            log_msg("[LOGIN]", "Rejeitado: nome duplicado."); // Log adicional
        } 
        else {
            // CORREÇÃO: Verifica se realmente conseguiu registar (se havia espaço)
            if (registar_cliente(m->pid, m->username)) {
                if (fd != -1) {
                    Mensagem resposta;
                    resposta.pid = getpid();
                    strcpy(resposta.comando, "login_ok");
                    strcpy(resposta.mensagem, "Login aceite.");
                    write(fd, &resposta, sizeof(Mensagem));
                    close(fd);
                }
                sprintf(msg_buf, "Cliente %s (PID %d) entrou.", m->username, m->pid);
                log_msg("[LOGIN]", msg_buf);
            } 
            else {
                // Se a função retornou 0, é porque não havia espaço
                if (fd != -1) {
                    Mensagem erro;
                    erro.pid = getpid();
                    strcpy(erro.comando, "erro");
                    strcpy(erro.mensagem, "Servidor cheio! Tente mais tarde.");
                    write(fd, &erro, sizeof(Mensagem));
                    close(fd);
                }
                log_msg("[LOGIN]", "Rejeitado: Servidor cheio.");
            }
        }
    }
    else if (strcmp(m->comando, "agendar") == 0) {
        if (sscanf(m->mensagem, "%d %99s %d", &h, loc, &d) == 3) {
            
            int novo_id = ctrl.proximo_id++;

            sprintf(msg_buf, "Pedido Agendar (ID %d): %s, %dkm, %dh", novo_id, loc, d, h);
            log_msg("[PEDIDO]", msg_buf);
            
            if (h < ctrl.tempo) {
                char erro_msg[100];
                sprintf(erro_msg, "Erro: Impossível agendar para %d (Atual: %d).", h, ctrl.tempo);
                enviar_resposta(m->pid, erro_msg);
            }
            else if (h == ctrl.tempo && ctrl.num_veiculos < NVEICULOS) {
                lancar_veiculo(m->username, m->pid, d, loc, novo_id);
                
                char resp[100];
                sprintf(resp, "Sucesso: Serviço ID %d iniciado de imediato!", novo_id);
                enviar_resposta(m->pid, resp);
            } 
            else {
                registar_agendamento_na_lista(novo_id, m->username, m->pid, h, d, loc);
                
                char confirm[100];
                sprintf(confirm, "Sucesso: Agendamento ID %d registado para t=%d.", novo_id, h);
                enviar_resposta(m->pid, confirm);
            }
        } else {
            enviar_resposta(m->pid, "Erro sintaxe. Use: agendar <hora> <local> <km>");
        }
    }
    else if (strcmp(m->comando, "consultar") == 0) {
        int encontrou = 0;
        char pipe_cli[100];
        sprintf(pipe_cli, PIPE_CLIENTE, m->pid);
        int fd_resp = open(pipe_cli, O_WRONLY | O_NONBLOCK);
        
        if (fd_resp != -1) {
            Mensagem resp;
            resp.pid = getpid();
            strcpy(resp.comando, "resposta");

            for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
                if (ctrl.agenda[i].ativo && ctrl.agenda[i].pid_cliente == m->pid) {
                    char buffer[256];
                    sprintf(buffer, "PENDENTE | ID %d | %dh | %s (%dkm)", 
                            ctrl.agenda[i].id, ctrl.agenda[i].hora, ctrl.agenda[i].local, ctrl.agenda[i].distancia);
                    strcpy(resp.mensagem, buffer);
                    write(fd_resp, &resp, sizeof(Mensagem));
                    encontrou = 1;
                }
            }
            for (int i = 0; i < ctrl.num_veiculos; i++) {
                if (ctrl.frota[i].pid > 0 && ctrl.frota[i].pid_cliente == m->pid) {
                    char buffer[256];
                    sprintf(buffer, "A DECORRER | ID %d | %s", 
                            ctrl.frota[i].id_servico, ctrl.frota[i].ultimo_status);
                    strcpy(resp.mensagem, buffer);
                    write(fd_resp, &resp, sizeof(Mensagem));
                    encontrou = 1;
                }
            }

            if (!encontrou) {
                strcpy(resp.mensagem, "Sem serviços ativos ou pendentes.");
                write(fd_resp, &resp, sizeof(Mensagem));
            }
            close(fd_resp);
        }
    }
    else if (strcmp(m->comando, "cancelar") == 0) {
        int id_alvo = atoi(m->mensagem);
        
        sprintf(msg_buf, "Cliente %s pede cancelamento ID=%d", m->username, id_alvo);
        log_msg("[PEDIDO]", msg_buf);
        
        int n = cancelar_servico(m->pid, id_alvo);
        
        char resp[100];
        if (n > 0) sprintf(resp, "Sucesso: %d serviços cancelados.", n);
        else sprintf(resp, "Erro: Serviço ID %d não encontrado ou não te pertence.", id_alvo);
        
        enviar_resposta(m->pid, resp);
    }
    else if (strcmp(m->comando, "terminar") == 0) {
        int ocupado = 0;
        for(int i=0; i<ctrl.num_veiculos; i++) {
            if (ctrl.frota[i].pid > 0 && ctrl.frota[i].pid_cliente == m->pid) {
                ocupado = 1; break;
            }
        }

        char pipe_cli[100];
        sprintf(pipe_cli, PIPE_CLIENTE, m->pid);
        int fd_resp = open(pipe_cli, O_WRONLY | O_NONBLOCK);
        
        if (fd_resp != -1) {
            Mensagem resp;
            resp.pid = getpid();
            if (ocupado) {
                strcpy(resp.comando, "erro");
                strcpy(resp.mensagem, "Tens viagens a decorrer! Cancela-as antes de sair.");
            } else {
                strcpy(resp.comando, "exit_ok");
                strcpy(resp.mensagem, "A desligar...");
                remover_cliente(m->pid); 
                sprintf(msg_buf, "Cliente %s saiu.", m->username);
                log_msg("[LOGOUT]", msg_buf);
            }
            write(fd_resp, &resp, sizeof(Mensagem));
            close(fd_resp);
        }
    }
}

void verificar_clientes() {
    Mensagem m;
    int n = read(ctrl.fd_clientes, &m, sizeof(Mensagem));
    if (n > 0) {
        processar_comando_cliente(&m);
    } else if (n == 0) {
        close(ctrl.fd_clientes);
        ctrl.fd_clientes = open(PIPE_CONTROLADOR, O_RDONLY | O_NONBLOCK);
    }
}

// ============================================================================
// INTERFACE ADMIN
// ============================================================================

void verificar_admin() {
    char cmd[100], arg[50] = "";
    int n = read(STDIN_FILENO, cmd, sizeof(cmd)-1);
    if (n > 0) {
        cmd[n] = '\0';
        if (cmd[strlen(cmd)-1] == '\n') cmd[strlen(cmd)-1] = '\0';

        char *token = strtok(cmd, " ");
        if (token == NULL) return;
        char *temp_arg = strtok(NULL, " ");
        if (temp_arg) strcpy(arg, temp_arg);

        if (strcmp(token, "listar") == 0) {
            printf("\n--- AGENDAMENTOS PENDENTES ---\n");
            int vazia = 1;
            for(int i=0; i<MAX_AGENDAMENTOS; i++) {
                if(ctrl.agenda[i].ativo) {
                    printf("ID %d | Cliente: %s | Hora: %d | Destino: %s\n", 
                           ctrl.agenda[i].id, ctrl.agenda[i].username, ctrl.agenda[i].hora, ctrl.agenda[i].local);
                    vazia = 0;
                }
            }
            if(vazia) printf("(Vazio)\n");
            printf("------------------------------\n");
        }
        else if (strcmp(token, "frota") == 0) {
            printf("\n--- ESTADO DA FROTA ---\n");
            int vazia = 1;
            for(int i=0; i<ctrl.num_veiculos; i++) {
                if(ctrl.frota[i].pid > 0) {
                    printf("Taxi %d [ID Serviço %d]: %s\n", 
                           ctrl.frota[i].pid, ctrl.frota[i].id_servico,
                           ctrl.frota[i].ultimo_status);
                    vazia = 0;
                }
            }
            if(vazia) printf("(Nenhum veículo ativo)\n");
            printf("-----------------------\n");
        }
        else if (strcmp(token, "cancelar") == 0) {
            if (strlen(arg) == 0) {
                printf("[ERRO] Uso: cancelar <ID_SERVICO> (ou 0 para tudo)\n");
            } else {
                int id_alvo = atoi(arg);
                printf("[ADMIN] A cancelar serviço ID %d (ou todos se 0)...\n", id_alvo);
                
                int num = cancelar_servico(-1, id_alvo);
                printf("[ADMIN] %d serviços cancelados.\n", num);
            }
        }
        else if (strcmp(token, "utiliz") == 0) {
            printf("\n--- UTILIZADORES ---\n");
            for(int i=0; i<NUTILIZADORES; i++)
                if(ctrl.clientes[i].pid > 0) printf("- %s (PID %d)\n", ctrl.clientes[i].username, ctrl.clientes[i].pid);
            printf("--------------------\n");
        }
        else if (strcmp(token, "km") == 0) printf("[ADMIN] Total KMs: %d\n", ctrl.total_km);
        else if (strcmp(token, "hora") == 0) printf("[ADMIN] Tempo Simulado: %d\n", ctrl.tempo);
        else if (strcmp(token, "terminar") == 0) exit(0);
        else {
            printf("[ERRO] Comando desconhecido: %s\n", token);
        }
        
        fflush(stdout);
    }
}

int main() {
    setup_inicial();
    while (1) {
        verificar_clientes(); 
        verificar_frota();
        verificar_agendamentos();
        verificar_admin();    
        sleep(1); 
        ctrl.tempo++;
    }
    return 0;
}