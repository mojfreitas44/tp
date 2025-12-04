#include "comum.h"

// Estrutura do Veículo (Frota)
typedef struct
{
    pid_t pid;
    int fd_leitura;
    int ocupado;
    pid_t pid_cliente;
    char ultimo_status[50];
    int distancia_viagem;
    int id_servico;
    pthread_t thread_id;
    int thread_finalizada;
    int tempo_conclusao_estimado;
} Veiculo;

// Estrutura de Agendamento (Lista de Espera)
typedef struct
{
    int id;
    char username[50];
    pid_t pid_cliente;
    int hora;
    int distancia;
    char local[100];
    int ativo; // 1 = Pendente, 0 = Vazio
    int ultimo_aviso;
    int aguardar_confirmacao;
    int hora_proposta;
} Agendamento;

// Estrutura de Informação do Cliente
typedef struct
{
    pid_t pid;
    char username[50];
} ClienteInfo;

// Estrutura Geral do Controlador
typedef struct
{
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

// mutex para sincronização
pthread_mutex_t m_clientes = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_frota = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_agenda = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_km = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_tempo = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// FUNÇÕES AUXILIARES GERAIS
// ============================================================================

void log_msg(const char *tag, const char *msg)
{
    int tempo;
    pthread_mutex_lock(&m_tempo);
    tempo = ctrl.tempo;
    pthread_mutex_unlock(&m_tempo);
    printf("[TEMPO %03d] %-12s %s\n", tempo, tag, msg);
    fflush(stdout);
}

void limpar_recursos()
{
    printf("\n[SISTEMA] A encerrar controlador e notificar todos...\n");
    pthread_mutex_lock(&m_clientes);
    for (int i = 0; i < NUTILIZADORES; i++)
    {
        if (ctrl.clientes[i].pid > 0)
        {
            kill(ctrl.clientes[i].pid, SIGUSR1);
        }
    }
    pthread_mutex_unlock(&m_clientes);

    pthread_mutex_lock(&m_frota);
    for (int i = 0; i < NVEICULOS; i++)
    {
        if (ctrl.frota[i].pid > 0)
        {
            kill(ctrl.frota[i].pid, SIGKILL);
            close(ctrl.frota[i].fd_leitura);
        }
    }
    pthread_mutex_unlock(&m_frota);

    if (ctrl.fd_clientes != -1)
        close(ctrl.fd_clientes);
    unlink(PIPE_CONTROLADOR);
}

void handler_sinal(int s)
{
    exit(0);
}

// CORREÇÃO: Agora retorna int (1=Sucesso, 0=Cheio)
int registar_cliente(pid_t pid, char *nome)
{
    pthread_mutex_lock(&m_clientes);
    for (int i = 0; i < NUTILIZADORES; i++)
    {
        if (ctrl.clientes[i].pid == 0)
        { // Encontrou slot vazio
            ctrl.clientes[i].pid = pid;
            strcpy(ctrl.clientes[i].username, nome); // ver diferença entre strcpy e strncpy
            pthread_mutex_unlock(&m_clientes);
            return 1; // Sucesso
        }
    }
    pthread_mutex_unlock(&m_clientes);
    return 0; // Lista cheia
}

void remover_cliente(pid_t pid)
{
    int cancelados = 0;
    pthread_mutex_lock(&m_clientes);
    for (int i = 0; i < NUTILIZADORES; i++)
    {
        if (ctrl.clientes[i].pid == pid)
        {
            ctrl.clientes[i].pid = 0;
            ctrl.clientes[i].username[0] = '\0';
            break;
        }
    }
    pthread_mutex_unlock(&m_clientes);

    // Cancelar agendamentos pendentes deste cliente
    pthread_mutex_lock(&m_agenda);
    for (int i = 0; i < MAX_AGENDAMENTOS; i++)
    {
        if (ctrl.agenda[i].ativo == 1 && ctrl.agenda[i].pid_cliente == pid)
        {
            ctrl.agenda[i].ativo = 0;
            cancelados++;
        }
    }
    pthread_mutex_unlock(&m_agenda);
    if (cancelados > 0)
    {
        char msg[100];
        sprintf(msg, "Cancelados %d agendamentos pendentes do cliente (PID %d).", cancelados, pid);
        log_msg("[CANCELAR]", msg);
    }
}

// thread para simulaer o tempo
// uso de mutex para porque é necessário ler e escrever o tempo
void *thread_relogio(void *arg)
{
    (void)arg;
    while (1)
    {
        sleep(1);
        pthread_mutex_lock(&m_tempo);
        ctrl.tempo++;
        pthread_mutex_unlock(&m_tempo);
    }
    return NULL;
}

void setup_inicial()
{
    setbuf(stdout, NULL);
    memset(&ctrl, 0, sizeof(Controlador));
    ctrl.fd_clientes = -1;
    ctrl.proximo_id = 1;

    int fd_check = open(PIPE_CONTROLADOR, O_WRONLY | O_NONBLOCK);
    if (fd_check != -1)
    {
        printf("[ERRO] Já existe uma instância do programa controlador em execução!\n");
        close(fd_check);
        exit(1);
    }

    signal(SIGINT, handler_sinal);
    atexit(limpar_recursos);

    if (mkfifo(PIPE_CONTROLADOR, 0666) == -1 && errno != EEXIST)
    {
        perror("[ERRO] Falha no mkfifo");
        exit(1);
    }

    ctrl.fd_clientes = open(PIPE_CONTROLADOR, O_RDONLY | O_NONBLOCK);
    if (ctrl.fd_clientes == -1)
    {
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

void enviar_resposta(pid_t pid_cli, const char *comando, const char *mensagem)
{
    char pipe_name[100];
    snprintf(pipe_name, sizeof(pipe_name), PIPE_CLIENTE, pid_cli);
    int fd = open(pipe_name, O_WRONLY | O_NONBLOCK);
    if (fd == -1)
    {
        log_msg("[AVISO]", "Não consegui abrir pipe do cliente");
        return;
    }

    Mensagem resp;
    memset(&resp, 0, sizeof(Mensagem));
    resp.pid = getpid();
    snprintf(resp.comando, sizeof(resp.comando), "%s", comando);
    snprintf(resp.mensagem, sizeof(resp.mensagem), "%s", mensagem);

    if (write(fd, &resp, sizeof(Mensagem)) == -1)
    {
        perror("Erro ao enviar resposta");
    }
    close(fd);
}

// ============================================================================
// GESTÃO DE AGENDAMENTOS E FROTA (IDs)
// ============================================================================

int registar_agendamento_na_lista(int id_servico, char *user, pid_t pid, int h, int d, char *loc, int executar)
{
    pthread_mutex_lock(&m_agenda);
    for (int i = 0; i < MAX_AGENDAMENTOS; i++)
    {
        if (ctrl.agenda[i].ativo == 0)
        {
            ctrl.agenda[i].id = id_servico;
            strcpy(ctrl.agenda[i].username, user);
            ctrl.agenda[i].pid_cliente = pid;
            ctrl.agenda[i].hora = h;
            ctrl.agenda[i].distancia = d;
            strcpy(ctrl.agenda[i].local, loc);
            ctrl.agenda[i].ativo = 1;
            ctrl.agenda[i].ultimo_aviso = -10;
            ctrl.agenda[i].aguardar_confirmacao = executar;

            char msg[100];
            sprintf(msg, "Agendado ID %d para t=%d (Slot %d)", id_servico, h, i);
            log_msg("[AGENDA]", msg);

            pthread_mutex_unlock(&m_agenda);
            return i;
        }
    }
    pthread_mutex_unlock(&m_agenda);
    log_msg("[ERRO]", "Lista de agendamentos cheia!");
    return -1;
}

int cancelar_servico(pid_t pid_solicitante, int id_cancelar)
{
    int cancelados = 0;
    pthread_mutex_lock(&m_frota);

    // --- 1. Cancelar Veículos em Andamento (FROTA) ---
    for (int i = 0; i < ctrl.num_veiculos; i++)
    {
        if (ctrl.frota[i].pid > 0)
        {
            int alvo = 0;

            if (pid_solicitante == -1)
            { // ADMIN
                if (id_cancelar == 0)
                    alvo = 1;
                else if (ctrl.frota[i].id_servico == id_cancelar)
                    alvo = 1;
            }
            else
            { // CLIENTE
                if (ctrl.frota[i].pid_cliente == pid_solicitante)
                {
                    if (id_cancelar == 0)
                        alvo = 1;
                    else if (ctrl.frota[i].id_servico == id_cancelar)
                        alvo = 1;
                }
            }

            if (alvo)
            {
                kill(ctrl.frota[i].pid, SIGUSR1);
                strcpy(ctrl.frota[i].ultimo_status, "A cancelar...");

                cancelados++;
                printf("[SISTEMA] Sinal de cancelamento enviado ao Veículo %d (Serviço ID %d).\n", ctrl.frota[i].pid, ctrl.frota[i].id_servico);
            }
        }
    }
    pthread_mutex_unlock(&m_frota);

    pthread_mutex_lock(&m_agenda);
    // --- 2. Cancelar Agendamentos Pendentes (AGENDA) ---
    for (int i = 0; i < MAX_AGENDAMENTOS; i++)
    {
        if (ctrl.agenda[i].ativo)
        {
            int alvo = 0;

            if (pid_solicitante == -1)
            { // ADMIN
                if (id_cancelar == 0)
                    alvo = 1;
                else if (ctrl.agenda[i].id == id_cancelar)
                    alvo = 1;
            }
            else
            { // CLIENTE
                if (ctrl.agenda[i].pid_cliente == pid_solicitante)
                {
                    if (id_cancelar == 0)
                        alvo = 1;
                    else if (ctrl.agenda[i].id == id_cancelar)
                        alvo = 1;
                }
            }

            if (alvo)
            {
                ctrl.agenda[i].ativo = 0;
                cancelados++;

                if (pid_solicitante == -1)
                {
                    char aviso[100];
                    sprintf(aviso, "O teu agendamento (ID %d) foi cancelado pelo Admin.", ctrl.agenda[i].id);
                    enviar_resposta(ctrl.agenda[i].pid_cliente, "cancelar", aviso);
                }

                printf("[SISTEMA] Agendamento ID %d removido da lista.\n", ctrl.agenda[i].id);
            }
        }
    }
    pthread_mutex_unlock(&m_agenda);
    return cancelados;
}

// ============================================================================
// GESTÃO DE VEÍCULOS
// ============================================================================

void *thread_veiculo(void *arg)
{
    Veiculo *v = (Veiculo *)arg;
    char buffer[256];
    int km_reportados = 0;

    while (1)
    {
        pthread_mutex_lock(&m_frota);
        int fd = v->fd_leitura;
        pthread_mutex_unlock(&m_frota);

        int n = read(fd, buffer, sizeof(buffer) - 1);
        if (n <= 0)
            break;

        buffer[n] = '\0';
        //printf("[VEÍCULO %d] %s", v->pid, buffer);
        

        char *ptr_relatorio = strstr(buffer, "[RELATORIO]");

        if (ptr_relatorio != NULL)
        {
            // Lemos o número a partir do ponteiro encontrado, ignorando o lixo antes
            if (sscanf(ptr_relatorio, "[RELATORIO] %d", &km_reportados) == 1)
            {
                pthread_mutex_lock(&m_km);
                ctrl.total_km += km_reportados;
                pthread_mutex_unlock(&m_km);

                // Confirmação visual para saberes que contou
                printf("[SISTEMA] Contabilizados +%d Km (Total: %d).\n",km_reportados, ctrl.total_km);
            }
        }

        if (strstr(buffer, "Progresso:") != NULL || strstr(buffer, "Início") != NULL)
        {
            pthread_mutex_lock(&m_frota);
            
            // Copia do buffer para a variável final
            strncpy(v->ultimo_status, buffer, sizeof(v->ultimo_status) - 1);
            
            // Remove o \n final para ficar bonito no comando 'frota'
            char *enter = strchr(v->ultimo_status, '\n');
            if (enter) *enter = '\0';
            
            pthread_mutex_unlock(&m_frota);
        }

    }
    pthread_mutex_lock(&m_frota);
    v->thread_finalizada = 1;
    pthread_mutex_unlock(&m_frota);
    return NULL;
}

int obter_proxima_vaga(){
    int menor_tempo_fim = 99999;
    int encontrou = 0;

    pthread_mutex_lock(&m_frota);
    for(int i = 0; i< NVEICULOS; i++){
        if(ctrl.frota[i].ocupado){
            if(ctrl.frota[i].tempo_conclusao_estimado < menor_tempo_fim){
                menor_tempo_fim = ctrl.frota[i].tempo_conclusao_estimado;
                encontrou = 1;
            }
        }else{
            pthread_mutex_unlock(&m_frota);
            return -1;
        }
    }
    pthread_mutex_unlock(&m_frota);

    if(encontrou){
        return menor_tempo_fim + 1; 
    }
    return ctrl.tempo + 10;

}

int lancar_veiculo(char *user, int pid_cli, int dist, char *local, int id_servico)
{
    int p[2];
    pid_t pid;
    char str_pid[20], str_dist[20], buffer[200];

    pthread_mutex_lock(&m_frota);
    int idx = -1;
    for (int i = 0; i < NVEICULOS; ++i)
    {
        if (!ctrl.frota[i].ocupado)
        {
            idx = i;
            ctrl.frota[i].ocupado = 1;
            break;
        }
    }
    pthread_mutex_unlock(&m_frota);

    if (idx == -1){
        printf("[DEBUG] FALHA: Não encontrei slot livre (NVEICULOS=%d)\n", NVEICULOS);
        return 0;
    }
    

    if (pipe(p) == -1)
    {
        
        log_msg("[ERRO]", "Falha pipe anónimo");
        pthread_mutex_lock(&m_frota);
        ctrl.frota[idx].ocupado = 0;
        pthread_mutex_unlock(&m_frota);

        return 0;
    }

    pid = fork();

    if (pid == 0)
    {
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
    }
    else if (pid > 0)
    {
        // --- PAI (CONTROLADOR) ---
        close(p[1]);
        

        pthread_mutex_lock(&m_frota);

        ctrl.frota[idx].pid = pid;
        ctrl.frota[idx].pid_cliente = pid_cli;
        ctrl.frota[idx].fd_leitura = p[0];
        ctrl.frota[idx].distancia_viagem = dist;
        ctrl.frota[idx].id_servico = id_servico;
        strcpy(ctrl.frota[idx].ultimo_status, "A iniciar");

        //calcula quando carro acaba
        pthread_mutex_lock(&m_tempo);
        int t_agora = ctrl.tempo;
        pthread_mutex_unlock(&m_tempo);

        ctrl.frota[idx].tempo_conclusao_estimado = t_agora + dist;

        if (pthread_create(&ctrl.frota[idx].thread_id, NULL, thread_veiculo, &ctrl.frota[idx]) != 0)
        {
            perror("[ERRO] Falha ao criar thread veiculo");
            exit(1);
        }

        ctrl.num_veiculos++;
        sprintf(buffer, "Veículo enviado (Serviço ID %d) para %s", id_servico, user);
        log_msg("[FROTA]", buffer);
        pthread_mutex_unlock(&m_frota);
        return 1;
    }
    else
    { // falha no fork
        perror("[ERRO] Fork falhou");   
        close(p[0]);
        close(p[1]);

        // Temos de libertar o lugar que reservámos
        pthread_mutex_lock(&m_frota);
        ctrl.frota[idx].ocupado = 0;
        pthread_mutex_unlock(&m_frota);
    }
    
    return 0;
}

void verificar_agendamentos(void)
{

    int tempo_atual;
    pthread_mutex_lock(&m_tempo);
    tempo_atual = ctrl.tempo;
    pthread_mutex_unlock(&m_tempo);

    pthread_mutex_lock(&m_agenda);
    for (int i = 0; i < MAX_AGENDAMENTOS; ++i)
    {
        if (ctrl.agenda[i].ativo && ctrl.agenda[i].hora <= tempo_atual && !ctrl.agenda[i].aguardar_confirmacao)
        {
            // copiar para variáveis locais
            char user[50];
            char local[100];
            int pid_cli = ctrl.agenda[i].pid_cliente;
            int dist = ctrl.agenda[i].distancia;
            int id_serv = ctrl.agenda[i].id;
            strcpy(user, ctrl.agenda[i].username);
            strcpy(local, ctrl.agenda[i].local);

            pthread_mutex_unlock(&m_agenda);

            if (lancar_veiculo(user, pid_cli, dist, local, id_serv))
            {
                pthread_mutex_lock(&m_agenda);
                ctrl.agenda[i].ativo = 0;
                enviar_resposta(pid_cli, "info", "Viatura a caminho.");
            }
            else
            {
                printf("Aqui");

                pthread_mutex_lock(&m_agenda); 
                if (tempo_atual - ctrl.agenda[i].ultimo_aviso >= 5) {
                    int proxima_vaga = obter_proxima_vaga();
                if(proxima_vaga <= tempo_atual)
                    proxima_vaga = tempo_atual + 5;
                    
                    char proposta[200];
                    sprintf(proposta, "Frota cheia. Aceitas reagendar ID %d para t=%d? (Escreve: decisao %d s)", id_serv, proxima_vaga, id_serv);
                    enviar_resposta(pid_cli, "status", proposta);

                    // MARCA COMO AGUARDANDO RESPOSTA
                    ctrl.agenda[i].aguardar_confirmacao = 1;
                    ctrl.agenda[i].hora_proposta = proxima_vaga;
                    ctrl.agenda[i].ultimo_aviso = tempo_atual;
                }
            }
        }
    }
    pthread_mutex_unlock(&m_agenda);
}



// ============================================================================
// GESTÃO DE PEDIDOS (CLIENTES)
// ============================================================================

void processar_comando_cliente(Mensagem *m)
{
    char msg_buf[300];
    int h, d;
    char loc[100];

    if (strcmp(m->comando, "login") == 0)
    {
        int existe = 0;
        pthread_mutex_lock(&m_clientes);
        for (int i = 0; i < NUTILIZADORES; i++)
        {
            if (ctrl.clientes[i].pid > 0 && strcmp(ctrl.clientes[i].username, m->username) == 0)
            {
                existe = 1;
                break;
            }
        }
        pthread_mutex_unlock(&m_clientes);

        char pipe_cli[100];
        sprintf(pipe_cli, PIPE_CLIENTE, m->pid);
        int fd = open(pipe_cli, O_WRONLY | O_NONBLOCK);

        if (existe)
        {
            if (fd != -1)
            {
                Mensagem erro;
                erro.pid = getpid();
                strcpy(erro.comando, "erro");
                sprintf(erro.mensagem, "Utilizador '%s' ja existe.", m->username);
                write(fd, &erro, sizeof(Mensagem));
                close(fd);
            }
            log_msg("[LOGIN]", "Rejeitado: nome duplicado."); // Log adicional
        }
        else
        {
            // CORREÇÃO: Verifica se realmente conseguiu registar (se havia espaço)
            if (registar_cliente(m->pid, m->username))
            {
                if (fd != -1)
                {
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
            else
            {
                // Se a função retornou 0, é porque não havia espaço
                if (fd != -1)
                {
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
    else if (strcmp(m->comando, "agendar") == 0)
    {
        if (sscanf(m->mensagem, "%d %99s %d", &h, loc, &d) == 3)
        {

            int novo_id;
            pthread_mutex_lock(&m_agenda);
            novo_id = ctrl.proximo_id++;
            pthread_mutex_unlock(&m_agenda);

            sprintf(msg_buf, "Pedido Agendar (ID %d): %s, %dkm, %dh", novo_id, loc, d, h);
            log_msg("[PEDIDO]", msg_buf);

            pthread_mutex_lock(&m_tempo);
            int tempo_atual = ctrl.tempo;
            pthread_mutex_unlock(&m_tempo);
            if (h < tempo_atual)
            {
                char erro_msg[100];
                sprintf(erro_msg, "Erro: Impossível agendar para %d (Atual: %d).", h, ctrl.tempo);
                enviar_resposta(m->pid, m->comando, erro_msg);
            }
            else if (h == tempo_atual)
            {
                if (lancar_veiculo(m->username, m->pid, d, loc, novo_id))
                {
                    char resp[100];
                    sprintf(resp, "Sucesso: Serviço ID %d iniciado de imediato!", novo_id);
                    enviar_resposta(m->pid, m->comando, resp);
                }
                else
                {
                    // FROTA CHEIA: Adicionar à lista 
                    int idx = registar_agendamento_na_lista(novo_id, m->username, m->pid, h, d, loc, 1);
                    if(idx != -1){
                        int proxima_vaga = obter_proxima_vaga();

                        if(proxima_vaga <= tempo_atual) 
                            proxima_vaga = tempo_atual + 2;
        
                            // 3. Modifica o agendamento que acabámos de criar para ficar "Bloqueado" à espera de resposta
                            pthread_mutex_lock(&m_agenda);
                            
                            ctrl.agenda[idx].aguardar_confirmacao = 1;
                            ctrl.agenda[idx].hora_proposta = proxima_vaga;
                            ctrl.agenda[idx].ultimo_aviso = tempo_atual;
                            
                            pthread_mutex_unlock(&m_agenda);
                            
                            char confirm[100];
                            sprintf(confirm, "Frota cheia! Agendamento ID %d colocado em espera prioritária.", novo_id);
                            enviar_resposta(m->pid, "aviso", confirm);
                    
                    }
                    
                }
            }
            else
            {
                int ocupados_na_hora = 0;

                pthread_mutex_lock(&m_frota);
                for(int i = 0; i<NVEICULOS; i++){
                    if(ctrl.frota[i].ocupado && ctrl.frota[i].tempo_conclusao_estimado > h){
                        ocupados_na_hora++;
                    }   
                }
                pthread_mutex_unlock(&m_frota);

                if(ocupados_na_hora >= NVEICULOS){
                    int idx = registar_agendamento_na_lista(novo_id, m->username, m->pid, h, d, loc, 1);
                
                    if (idx == -1)
                    {
                        int proxima_vaga = obter_proxima_vaga();

                        if(proxima_vaga <= h)
                            proxima_vaga = h +5;
                            
                        pthread_mutex_lock(&m_agenda);
                        ctrl.agenda[idx].aguardar_confirmacao = 1;
                        ctrl.agenda[idx].hora_proposta = proxima_vaga;
                        ctrl.agenda[idx].ultimo_aviso = tempo_atual;
                        pthread_mutex_unlock(&m_agenda);

                        char confirm[200];
                        sprintf(confirm, "Previsão: Frota cheia em t=%d. Aceitas reagendar ID %d para t=%d? (decisao %d s)", h, novo_id, proxima_vaga, novo_id);
                        enviar_resposta(m->pid, "status", confirm);

                    }
                }else {
                    registar_agendamento_na_lista(novo_id, m->username, m->pid, h, d, loc, 0);
                    
                    char confirm[100];
                    sprintf(confirm, "Sucesso: Agendamento ID %d registado para t=%d.", novo_id, h);
                    enviar_resposta(m->pid, m->comando, confirm);
                }
                
            }
        }
        else
        {
            enviar_resposta(m->pid, m->comando, "Erro sintaxe. Use: agendar <hora> <local> <km>");
        }
    }
    else if (strcmp(m->comando, "consultar") == 0)
    {
        int encontrou = 0;
        char pipe_cli[100];
        snprintf(pipe_cli, sizeof(pipe_cli), PIPE_CLIENTE, m->pid);
        int fd_resp = open(pipe_cli, O_WRONLY | O_NONBLOCK);

        if (fd_resp != -1)
        {
            Mensagem resp;
            resp.pid = getpid();
            strcpy(resp.comando, "resposta");

            pthread_mutex_lock(&m_agenda);
            for (int i = 0; i < MAX_AGENDAMENTOS; i++)
            {
                if (ctrl.agenda[i].ativo && ctrl.agenda[i].pid_cliente == m->pid)
                {
                    char buffer[256];
                    sprintf(buffer, "PENDENTE | ID %d | %dh | %s (%dkm)",
                            ctrl.agenda[i].id, ctrl.agenda[i].hora, ctrl.agenda[i].local, ctrl.agenda[i].distancia);
                    strcpy(resp.mensagem, buffer);
                    write(fd_resp, &resp, sizeof(Mensagem));
                    encontrou = 1;
                }
            }
            pthread_mutex_unlock(&m_agenda);

            pthread_mutex_lock(&m_frota);
            for (int i = 0; i < ctrl.num_veiculos; i++)
            {
                if (ctrl.frota[i].pid > 0 && ctrl.frota[i].pid_cliente == m->pid)
                {
                    char buffer[256];
                    sprintf(buffer, "A DECORRER | ID %d | %s",
                            ctrl.frota[i].id_servico, ctrl.frota[i].ultimo_status);
                    strcpy(resp.mensagem, buffer);
                    write(fd_resp, &resp, sizeof(Mensagem));
                    encontrou = 1;
                }
            }
            pthread_mutex_unlock(&m_frota);

            if (!encontrou)
            {
                strcpy(resp.mensagem, "Sem serviços ativos ou pendentes.");
                write(fd_resp, &resp, sizeof(Mensagem));
            }
            close(fd_resp);
        }
    }
    else if (strcmp(m->comando, "cancelar") == 0)
    {
        int id_alvo = atoi(m->mensagem);

        sprintf(msg_buf, "Cliente %s pede cancelamento ID=%d", m->username, id_alvo);
        log_msg("[PEDIDO]", msg_buf);

        if (id_alvo == 0 && strcmp(m->mensagem, "0") != 0)
        {
            enviar_resposta(m->pid, "erro", "ID inválido! Insira um número.");
        }
        else
        {
            // 2. Tenta cancelar no sistema
            int n = cancelar_servico(m->pid, id_alvo);

            char resp[100];
            if (n > 0)
            {
                // SUCESSO: Envia com tag "info" (ou usa o default que vai cair no [CONTROLADOR])
                sprintf(resp, "Sucesso: Cancelaste o serviço ID %d.", id_alvo);
                enviar_resposta(m->pid, "cancelar", resp);
            }
            else
            {
                // FALHA: Envia com tag "erro" para o cliente mostrar a vermelho/[ERRO]
                sprintf(resp, "O serviço ID %d não existe ou não te pertence.", id_alvo);
                enviar_resposta(m->pid, "erro", resp);
            }
        }
    }
    else if (strcmp(m->comando, "terminar") == 0)
    {
        int ocupado = 0;
        pthread_mutex_lock(&m_frota);
        for (int i = 0; i < ctrl.num_veiculos; i++)
        {
            if (ctrl.frota[i].pid > 0 && ctrl.frota[i].pid_cliente == m->pid)
            {
                ocupado = 1;
                break;
            }
        }
        pthread_mutex_unlock(&m_frota);

        char pipe_cli[100];
        sprintf(pipe_cli, PIPE_CLIENTE, m->pid);
        int fd_resp = open(pipe_cli, O_WRONLY | O_NONBLOCK);

        if (fd_resp != -1)
        {
            Mensagem resp;
            resp.pid = getpid();
            if (ocupado)
            {
                strcpy(resp.comando, "erro");
                strcpy(resp.mensagem, "Tens viagens a decorrer! Cancela-as antes de sair.");
            }
            else
            {
                strcpy(resp.comando, "exit_ok");
                strcpy(resp.mensagem, "A desligar...");
                remover_cliente(m->pid);
                sprintf(msg_buf, "Cliente %s saiu.", m->username);
                log_msg("[LOGOUT]", msg_buf);
            }
            write(fd_resp, &resp, sizeof(Mensagem));
            close(fd_resp);
        }
    } else if(strcmp (m ->comando, "decisao") == 0){
        int id_alvo;
        char respo;

        printf("[DEBUG] Recebi decisao: '%s'\n", m->mensagem);
        if(sscanf(m->mensagem, "%d %c", &id_alvo, &respo) == 2){
            int encontrou = 0;
            pthread_mutex_lock(&m_agenda);
            
            for(int i=0; i<MAX_AGENDAMENTOS; i++){
                if(ctrl.agenda[i].ativo && ctrl.agenda[i].id == id_alvo && ctrl.agenda[i].pid_cliente == m->pid){
                    encontrou =1;
                    if(respo == 's' || respo == 'S'){
                        ctrl.agenda[i].hora = ctrl.agenda[i].hora_proposta;
                        ctrl.agenda[i].aguardar_confirmacao = 0;
                        
                        char confirma[100];
                        sprintf(confirma, "Reagendamento confirmado para t=%d.", ctrl.agenda[i].hora);
                        enviar_resposta(m->pid, "info", confirma);
                        
                        sprintf(msg_buf, "Agendamento ID %d reagendado para t=%d pelo cliente.", id_alvo, ctrl.agenda[i].hora);
                        log_msg("[AGENDA]", msg_buf);

                    }else{
                        ctrl.agenda[i].ativo = 0;

                        enviar_resposta(m->pid, "info", "Pedido cancelado a seu pedido.");
                        log_msg("[AGENDA]", "Cliente recusou reagendamento. Pedido removido.");
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&m_agenda);

            if(!encontrou){
                enviar_resposta(m->pid, "erro", "Pedido não encontrado ou não requer decisão.");
            }
        }else{
            enviar_resposta(m->pid, "erro", "Erro sintaxe. Use: decisao <ID> <s/n>");
        }
    }
}

void *thread_clientes(void *arg)
{
    (void)arg;
    Mensagem m;
    while (1)
    {
        if (ctrl.fd_clientes != -1)
        {
            int n = read(ctrl.fd_clientes, &m, sizeof(Mensagem));
            if (n > 0)
            {
                processar_comando_cliente(&m);
            }
            else if (n == 0)
            {
                close(ctrl.fd_clientes);
                ctrl.fd_clientes = open(PIPE_CONTROLADOR, O_RDONLY | O_NONBLOCK);
            }
            else if (n < 0 && errno != EAGAIN)
            {
                perror("[ERRO] leitura fifo clientes");
            }
        }
        usleep(10000);
    }
    return NULL;
}

// ============================================================================
// INTERFACE ADMIN
// ============================================================================

void *thread_admin(void *arg)
{
    (void)arg;
    char cmd[100];
    while (1)
    {

        if (fgets(cmd, sizeof(cmd), stdin) == NULL)
        {
            clearerr(stdin);
            usleep(100000);
            continue;
        }
        size_t L = strlen(cmd);
        if (L > 0 && cmd[L - 1] == '\n')
            cmd[L - 1] = '\0';
        char *token = strtok(cmd, " ");
        char *param = strtok(NULL, " ");

        if (!token)
            continue;

        if (strcmp(token, "listar") == 0)
        {
            printf("\n--- AGENDAMENTOS PENDENTES ---\n");
            int vazia = 1;
            pthread_mutex_lock(&m_agenda);
            for (int i = 0; i < MAX_AGENDAMENTOS; i++)
            {
                if (ctrl.agenda[i].ativo)
                {
                    printf("ID %d | Cliente: %s | Hora: %d | Destino: %s\n",
                           ctrl.agenda[i].id, ctrl.agenda[i].username, ctrl.agenda[i].hora, ctrl.agenda[i].local);
                    vazia = 0;
                }
            }
            pthread_mutex_unlock(&m_agenda);
            if (vazia)
                printf("(Vazio)\n");
            printf("------------------------------\n");
        }
        else if (strcmp(token, "frota") == 0)
        {
            printf("\n--- ESTADO DA FROTA ---\n");
            int vazia = 1;
            pthread_mutex_lock(&m_frota);
            for (int i = 0; i < ctrl.num_veiculos; i++)
            {
                if (ctrl.frota[i].pid > 0)
                {
                    printf("Taxi %d [ID Serviço %d]: %s\n",
                           ctrl.frota[i].pid, ctrl.frota[i].id_servico,
                           ctrl.frota[i].ultimo_status);
                    vazia = 0;
                }
            }
            pthread_mutex_unlock(&m_frota);
            if (vazia)
                printf("(Nenhum veículo ativo)\n");
            printf("-----------------------\n");
        }
        else if (strcmp(token, "cancelar") == 0)
        {
            if (!param)
            {
                printf("[ERRO] Uso: cancelar <ID_SERVICO> (ou 0 para tudo)\n");
            }
            else
            {
                int id_alvo = atoi(param);
                printf("[ADMIN] A cancelar serviço ID %d (ou todos se 0)...\n", id_alvo);

                int num = cancelar_servico(-1, id_alvo);
                printf("[ADMIN] %d serviços cancelados.\n", num);
            }
        }
        else if (strcmp(token, "utiliz") == 0)
        {
            printf("\n--- UTILIZADORES ---\n");
            pthread_mutex_lock(&m_clientes);
            for (int i = 0; i < NUTILIZADORES; i++)
                if (ctrl.clientes[i].pid > 0)
                    printf("- %s (PID %d)\n", ctrl.clientes[i].username, ctrl.clientes[i].pid);
            pthread_mutex_unlock(&m_clientes);
            printf("--------------------\n");
        }
        else if (strcmp(token, "km") == 0)
        {
            pthread_mutex_lock(&m_km);
            printf("[ADMIN] Total KMs: %d\n", ctrl.total_km);
            pthread_mutex_unlock(&m_km);
        }
        else if (strcmp(token, "hora") == 0)
        {
            pthread_mutex_lock(&m_tempo);
            printf("[ADMIN] Tempo Simulado: %d\n", ctrl.tempo);
            pthread_mutex_unlock(&m_tempo);
        }
        else if (strcmp(token, "terminar") == 0)
            exit(0);
        else
        {
            printf("[ERRO] Comando desconhecido: %s\n", token);
        }

        fflush(stdout);
    }
    return NULL;
}

int main()
{

    setup_inicial();
    pthread_t t_admin, t_clientes, t_relogio;
    if (pthread_create(&t_admin, NULL, thread_admin, NULL) != 0)
    {
        perror("[ERRO] Falha ao criar thread admin");
        exit(1);
    }
    if (pthread_create(&t_clientes, NULL, thread_clientes, NULL) != 0)
    {
        perror("[ERRO] Falha ao criar thread clientes");
        exit(1);
    }
    if (pthread_create(&t_relogio, NULL, thread_relogio, NULL) != 0)
    {
        perror("[ERRO] Falha ao criar thread relogio");
        exit(1);
    }

    while (1)
    {
        //verificar_agendamentos();
        for (int i = 0; i < NVEICULOS; i++)
        {
            int should_join = 0;
            pthread_mutex_lock(&m_frota);
            if (ctrl.frota[i].pid > 0 && ctrl.frota[i].thread_finalizada)
                should_join = 1;
            pthread_mutex_unlock(&m_frota);

            if (should_join)
            {
                /* copiar tid e fd para fora do lock */
                pthread_t tid;
                int fd;
                pid_t pidv;

                pthread_mutex_lock(&m_frota);
                tid = ctrl.frota[i].thread_id;
                fd = ctrl.frota[i].fd_leitura;
                pidv = ctrl.frota[i].pid;
                pthread_mutex_unlock(&m_frota);

                /* join (SEM mutexes bloqueados) */
                pthread_join(tid, NULL);

                /* cleanup do slot */
                if (fd > 0)
                    close(fd);
                pthread_mutex_lock(&m_frota);
                ctrl.frota[i].pid = 0;
                //ctrl.frota[i].fd_leitura = -1;  
                ctrl.frota[i].ocupado = 0;
                ctrl.frota[i].thread_finalizada = 0;
                ctrl.num_veiculos--;
                pthread_mutex_unlock(&m_frota);

                char buf[128];
                snprintf(buf, sizeof(buf), "Veículo slot %d (PID %d) terminado e recolhido.", i, (int)pidv);
                log_msg("[FROTA]", buf);
            }
        }
        verificar_agendamentos();
    }

    return 0;
}