#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<errno.h>
#include<unistd.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sqlite3.h>
#include<pthread.h>
#include<stdint.h>
#include<sys/time.h>
#include<sys/select.h>

#define PORT 2024
#define TIMER 20
#define MAX_PLAYERS 100
#define MAX_PARTIDE 100
#define MAX_MSG_SIZE 20000
#define MAX_ROW_SIZE 256
#define MAX_INTREBARE_SIZE 9000

extern int errno;

struct Jucator 
{
    char nickname[100];
    int scor;
    int client_socket;
};

struct Partida 
{
    int id;  
    struct Jucator jucatori[3];  
    int numar_jucatori; 
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int raspunsuri_primite;
    int intrebare_curenta;
    struct timeval start_time;
};

struct Partida partide[MAX_PARTIDE];
int nr_partide = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void adauga_jucator_partida(struct Jucator jucator) 
{
    pthread_mutex_lock(&lock);
    for(int i = 0; i < nr_partide; i++) 
    {
        if(partide[i].numar_jucatori < 3) 
        {
            partide[i].jucatori[partide[i].numar_jucatori] = jucator;
            partide[i].numar_jucatori++;
            if(partide[i].numar_jucatori == 3) 
            {
                printf("[server] Partida %d este completa. Se asteapta 2 secunde pentru mesajul de bun venit...\n", partide[i].id);
                usleep(2000000);
                printf("[server] Se incepe partida!\n");
                pthread_cond_broadcast(&partide[i].cond);
            }
            pthread_mutex_unlock(&lock);
            return;
        }
    }
    
    struct Partida p = {0};
    p.id = nr_partide;
    p.jucatori[0] = jucator;
    p.numar_jucatori = 1;
    p.intrebare_curenta = 1;
    p.raspunsuri_primite = 0;
    pthread_mutex_init(&p.lock, NULL);
    pthread_cond_init(&p.cond, NULL);
    partide[nr_partide] = p;
    nr_partide++;
    pthread_mutex_unlock(&lock);
}
void asteapta_inceperea_rundei(struct Partida *partida) 
{
    pthread_mutex_lock(&partida->lock);
    while(partida->numar_jucatori < 3) 
    {
        pthread_cond_wait(&partida->cond, &partida->lock);
    }
    pthread_mutex_unlock(&partida->lock);
}

void add_points(struct Partida *partida, const char *nickname, int points) 
{
    pthread_mutex_lock(&partida->lock);
    for(int i = 0; i < partida->numar_jucatori; i++) 
    {
        if(strcmp(partida->jucatori[i].nickname, nickname) == 0) 
        {
            partida->jucatori[i].scor += points;
            break;
        }
    }
    pthread_mutex_unlock(&partida->lock);
}

void delete_player(struct Partida *partida, const char *nickname) 
{
    pthread_mutex_lock(&partida->lock);
    int found_idx = -1;
    for(int i = 0; i < partida->numar_jucatori; i++) 
    {
        if(strcmp(partida->jucatori[i].nickname, nickname) == 0) 
        {
            found_idx = i;
            break;
        }
    }
    if(found_idx != -1) 
    {
        struct Jucator player_quit = partida->jucatori[found_idx];
        
        for(int j = found_idx; j < partida->numar_jucatori-1; j++) {
            partida->jucatori[j] = partida->jucatori[j+1];
        }
        partida->jucatori[partida->numar_jucatori-1] = player_quit;
        partida->numar_jucatori--;
        
        printf("[server] Jucatorul %s a parasit partida cu scorul %d!\n", nickname, player_quit.scor);

        if (partida->raspunsuri_primite == partida->numar_jucatori) 
        {
            partida->intrebare_curenta++;
            partida->raspunsuri_primite = 0;
            pthread_cond_broadcast(&partida->cond);
        } else if (partida->numar_jucatori == 0) 
        {
            pthread_cond_broadcast(&partida->cond);
        }
    }
    pthread_mutex_unlock(&partida->lock);
}

int compare_points(const void *a, const void *b) 
{
    return ((struct Jucator *)b)->scor - ((struct Jucator *)a)->scor;
}

void leaderboard(struct Partida *partida) 
{
    printf("[DEBUG-LEADERBOARD] Incepe generarea leaderboard pentru partida %d\n", partida->id);
    
    if(partida->numar_jucatori <= 0 && strlen(partida->jucatori[0].nickname) == 0) 
    {
        printf("[DEBUG-LEADERBOARD] Nu sunt jucatori, se returneaza\n");
        return;
    }
    
    struct Jucator *all_players = malloc(3 * sizeof(struct Jucator));
    if (!all_players) 
    {
        printf("[DEBUG-LEADERBOARD] Eroare la alocarea memoriei\n");
        return;
    }
    
    int total_players = 0;
    
    for(int i = 0; i < 3; i++) 
    {
        if(strlen(partida->jucatori[i].nickname) > 0) 
        {
            all_players[total_players++] = partida->jucatori[i];
        }
    }

    if(total_players > 0) 
    {
        qsort(all_players, total_players, sizeof(struct Jucator), compare_points);
    }
    
    char buffer[MAX_MSG_SIZE];
    strcpy(buffer, "LEADERBOARD PENTRU PARTIDA:\n");
    
    for(int i = 0; i < total_players; i++) 
    {
        char row[MAX_ROW_SIZE];
        const char *status = "";
        for(int j = 0; j < partida->numar_jucatori; j++) 
        {
            if(strcmp(all_players[i].nickname, partida->jucatori[j].nickname) == 0) 
            {
                status = "";
                break;
            }
            if(j == partida->numar_jucatori - 1) 
            {
                status = " (a parasit jocul)";
            }
        }
        snprintf(row, MAX_ROW_SIZE, "Locul %d: %s - %d puncte%s\n", i+1, all_players[i].nickname, all_players[i].scor, status);
        strcat(buffer, row);
    }
    
    strcat(buffer, "\n");
    strcat(buffer, "GAME OVER!\n");
    for(int i = 0; i < partida->numar_jucatori; i++) 
    {
        printf("[DEBUG-LEADERBOARD] Trimitere catre %s...\n", partida->jucatori[i].nickname);
        if(write(partida->jucatori[i].client_socket, buffer, strlen(buffer)+1) <= 0) 
        {
            printf("[DEBUG-LEADERBOARD] Eroare la trimitere catre %s\n", partida->jucatori[i].nickname);
        } 
        else 
        {
            printf("[DEBUG-LEADERBOARD] Trimis cu succes catre %s\n", partida->jucatori[i].nickname);
        }
    }
    free(all_players);
    printf("[DEBUG-LEADERBOARD] Leaderboard complet\n");
}
int create_socket() 
{
    int sd;
    if((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
    {
        perror("[server] Eroare la crearea socketului!\n");
        exit(1);
    }
    return sd;
}

int get_question(sqlite3 *db, int id, char *question, char *varA, char *varB, char *varC, char *varD, char *var_corecta, char *dificultate) 
{
    sqlite3_stmt *stmt;
    const char *sql = "SELECT intrebare,varianta_A,varianta_B,varianta_C,varianta_D,varianta_corecta,dificultate FROM intrebari WHERE id = ?";

    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) 
    {
        printf("Eroare la interogarea: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) 
    {
        strcpy(question, (const char*)sqlite3_column_text(stmt, 0));
        strcpy(varA, (const char*)sqlite3_column_text(stmt, 1));
        strcpy(varB, (const char*)sqlite3_column_text(stmt, 2));
        strcpy(varC, (const char*)sqlite3_column_text(stmt, 3));
        strcpy(varD, (const char*)sqlite3_column_text(stmt, 4));
        strcpy(var_corecta, (const char*)sqlite3_column_text(stmt, 5));
        strcpy(dificultate, (const char*)sqlite3_column_text(stmt, 6));
    } 
    else 
    {
        printf("S-au terminat intrebarile!\n");
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

void *handle_client(void *arg) 
{
    int client_socket = (intptr_t)arg;
    sqlite3 *db;
    
    if(sqlite3_open("questions.db", &db)) 
    {
        fprintf(stderr, "[server] Nu s-a putut deschide baza de date: %s\n", sqlite3_errmsg(db));
        close(client_socket);
        return NULL;
    }
    
    char msg[MAX_MSG_SIZE];
    char raspuns[MAX_MSG_SIZE];
    char nickname[MAX_ROW_SIZE];
    char intrebare[MAX_INTREBARE_SIZE];
    char varA[MAX_ROW_SIZE], varB[MAX_ROW_SIZE], varC[MAX_ROW_SIZE], varD[MAX_ROW_SIZE];
    char var_corecta[2], dificultate[MAX_ROW_SIZE];
    
    snprintf(msg, sizeof(msg), "Bine ai venit in aplicatia QUIZZ GAME!\n\n"
             "Jocul consta in raspunderea la 15 intrebari de cultura generala pe categorii diferite de dificultate si anume:\n"
             "1. Easy (2pcte)\n"
             "2. Medium (4pcte)\n"
             "3. Hard (6pcte)\n"
             "Pentru fiecare intrebare vei avea la dispozitie 20 secunde pentru a raspunde!\n\n");
             
    if(write(client_socket, msg, strlen(msg) + 1) <= 0) 
    {
        perror("[server] Eroare la write() catre client.\n");
        close(client_socket);
        return NULL;
    }
    
    if(read(client_socket, nickname, sizeof(nickname)) <= 0) 
    {
        perror("[server] Eroare la read() de la client!\n");
        close(client_socket);
        return NULL;
    }
    printf("[server] Nickname primit: %s\n", nickname);
    
    struct Jucator jucator;
    strcpy(jucator.nickname, nickname);
    jucator.scor = 0;
    jucator.client_socket = client_socket;
    adauga_jucator_partida(jucator);
    
    struct Partida *partida = NULL;
    pthread_mutex_lock(&lock);
    for(int i = 0; i < nr_partide; i++) 
    {
        for(int j = 0; j < 3; j++) 
        {
            if(strcmp(partide[i].jucatori[j].nickname, nickname) == 0) 
            {
                partida = &partide[i];
                break;
            }
        }
        if(partida) break;
    }
    pthread_mutex_unlock(&lock);
    
    asteapta_inceperea_rundei(partida);
    printf("[server] Partida %d a inceput pentru %s\n", partida->id, nickname);
    
    pthread_mutex_lock(&partida->lock);
    int id = partida->intrebare_curenta;
    pthread_mutex_unlock(&partida->lock);
    
    while(1) 
    {
        pthread_mutex_lock(&partida->lock);
        if(partida->numar_jucatori == 0)
        {
            char msg[] = "Toti jucatorii au parasit jocul!\n";
            write(client_socket,msg,strlen(msg)+1);
            pthread_mutex_unlock(&partida->lock);
            break;
        }
        id = partida->intrebare_curenta;
        pthread_mutex_unlock(&partida->lock);
        if(get_question(db, id, intrebare, varA, varB, varC, varD, var_corecta, dificultate) != 0) 
        {
            break;
        }
        snprintf(msg, sizeof(msg), "\n[%s] %d. %s\nA. %s\nB. %s\nC. %s\nD. %s\n", dificultate, id, intrebare, varA, varB, varC, varD);
                 
        if(write(client_socket, msg, strlen(msg) + 1) <= 0) 
        {
            perror("[server] Eroare la scriere catre client!\n");
            delete_player(partida, nickname);
            break;
        }
        pthread_mutex_lock(&partida->lock);
        partida->raspunsuri_primite++;
         printf("[DEBUG-%s] Astept raspunsuri pentru intrebarea %d (%d/%d jucatori)\n", nickname, id, partida->raspunsuri_primite, partida->numar_jucatori);
        if (partida->raspunsuri_primite == partida->numar_jucatori) 
        {
            gettimeofday(&partida->start_time, NULL);
            partida->raspunsuri_primite = 0;
            pthread_cond_broadcast(&partida->cond);
        } 
        else 
        {
            pthread_cond_wait(&partida->cond, &partida->lock);
        }
        pthread_mutex_unlock(&partida->lock);

        struct timeval timer;
        timer.tv_sec = TIMER;
        timer.tv_usec = 0;
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        
        int activitate = select(client_socket + 1, &read_fds, NULL, NULL, &timer);
        
        if(activitate <= 0) 
        {
            printf("[server] Jucatorul %s nu a raspuns la timp.\n", nickname);
            snprintf(msg, sizeof(msg), "Timpul a expirat! Raspunsul corect era: %s\n", var_corecta);
            if(write(client_socket, msg, strlen(msg) + 1) <= 0) 
            {
                perror("[server] Eroare la write() catre client.\n");
                break;
            }
            
            char buffer[1024];
            while(recv(client_socket, buffer, sizeof(buffer), MSG_DONTWAIT) > 0);
            
        } 
        else 
        {
            if(read(client_socket, raspuns, sizeof(raspuns)) <= 0) 
            {
                perror("[server] Eroare la read() de la client!\n");
                break;
            }
            
            if(strcmp(raspuns, "quit") == 0) 
            {
                printf("[server] Jucatorul %s a parasit jocul.\n", nickname);
                delete_player(partida, nickname);
                close(client_socket);
                sqlite3_close(db);
                return NULL;
            }
            if(strcmp(raspuns,"TIMEOUT") == 0)
            {
                snprintf(msg,sizeof(msg),"Timpul a expirat! Raspunsul corect era: %s\n",var_corecta);
                if(write(client_socket,msg,strlen(msg)+1)<=0)
                {
                    perror("[server] Eroare la write() catre client.\n");
                    exit(1);
                }
            }
            else
            {
                if(strcmp(raspuns, var_corecta) == 0) 
                {
                    snprintf(msg, sizeof(msg), "Raspuns corect! Bravo!\n");
                    int points = strcmp(dificultate, "Easy") == 0 ? 2 :
                                strcmp(dificultate, "Medium") == 0 ? 4 : 6;
                    add_points(partida, nickname, points);
                } 
                else 
                {
                    snprintf(msg, sizeof(msg), "Raspuns gresit! Raspunsul corect era: %s\n", var_corecta);
                }
                if(write(client_socket, msg, strlen(msg) + 1) <= 0) {
                    perror("[server] Eroare la write() catre client!\n");
                    break;
                }
            }
        }
        pthread_mutex_lock(&partida->lock);
        partida->raspunsuri_primite++;
        printf("[DEBUG-%s] Raspunsuri procesate pentru intrebarea %d (%d/%d jucatori)\n", nickname, id, partida->raspunsuri_primite, partida->numar_jucatori);
        if (partida->raspunsuri_primite == partida->numar_jucatori) 
        {
            usleep(800000);
            partida->intrebare_curenta++;
            partida->raspunsuri_primite = 0;
            printf("[DEBUG-%s] Toti au raspuns. Urmatoarea intrebare va fi %d\n", nickname, partida->intrebare_curenta);
            pthread_cond_broadcast(&partida->cond);
        } 
        else 
        {
            pthread_cond_wait(&partida->cond, &partida->lock);
        }
        pthread_mutex_unlock(&partida->lock);
    }
    printf("[debug - %s] Am iesit din while loop, intrebarile s-au terminat\n",nickname);
    pthread_mutex_lock(&partida->lock);
    if(partida->numar_jucatori > 0)
    {
        partida->raspunsuri_primite++;
        printf("[debug-%s] Raspunsuri primite dupa joc: %d/%d\n",nickname,partida->raspunsuri_primite,partida->numar_jucatori);
        if(partida->raspunsuri_primite == partida->numar_jucatori) 
        {
            printf("[DEBUG-%s] Toti jucatorii au terminat, se trimite leaderboard\n", nickname);
            leaderboard(partida);
            printf("[DEBUG-%s] Leaderboard trimis, se face broadcast\n", nickname);
            pthread_cond_broadcast(&partida->cond);
        } 
        else 
        {
            printf("[DEBUG-%s] Se asteapta si ceilalti jucatori\n", nickname);
            pthread_cond_wait(&partida->cond, &partida->lock);
        }
    }
    else
    {
        printf("[DEBUG-%s] Nu mai sunt jucatori activi.\n",nickname);
        char msg[] = "Jocul s-a incheiat - toti jucatorii au parasit partida.\n";
        write(client_socket,msg,strlen(msg)+1);
    }
    pthread_mutex_unlock(&partida->lock);
    
    printf("[DEBUG-%s] Se inchide conexiunea\n", nickname);
    close(client_socket);
    sqlite3_close(db);
    return NULL;
}
void setup_server(struct sockaddr_in *server,struct sockaddr_in *from)
{
    bzero(server,sizeof(*server));
    bzero(from,sizeof(*from));

    server->sin_family = AF_INET;
    server->sin_addr.s_addr=htonl(INADDR_ANY);
    server->sin_port=htons(PORT);
}
int main()
{
    struct sockaddr_in server;
    struct sockaddr_in from;
    int sd = create_socket();
    setup_server(&server,&from);

    if(bind(sd,(struct sockaddr *)&server,sizeof(struct sockaddr)) == -1)
    {
        perror("[server] Eroare la bind().\n");
        exit(1);
    }

    if(listen(sd,5)==-1)
    {
        perror("[server] Eroare la listen()\n");
        exit(1);
    }

    printf("[server] Asteptam la portul %d...\n",PORT);
    fflush(stdout);
    while(1)
    {
        int client;
        socklen_t length=sizeof(from);
        client = accept(sd,(struct sockaddr *)&from,&length);
        if (client < 0)
        {
            perror("[server] Eroare la accept().\n");
            continue;
        }
        printf("[server] Client conectat.\n");
        pthread_t tid;
        pthread_create(&tid,NULL,handle_client,(void *)(intptr_t)client);
        pthread_detach(tid);
    }
    close(sd);
    return 0;
}
