#include <gtk/gtk.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>
#include<stdbool.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *nickname_entry;
    GtkWidget *connect_button;
    GtkWidget *question_label;
    GtkWidget *answer_buttons[4];
    GtkWidget *status_label;
    GtkWidget *progress_bar;
    int socket_fd;
    guint timer_id;
    int remaining_time;
    gboolean waiting_for_response;
    gboolean game_over;
    GtkWidget *last_clicked_button;
    int response_order;
    GtkWidget *quit_button;
    GtkWidget *close_button;
} AppData;

static AppData app;

static int set_socket_nonblocking(int socket_fd) 
{
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) 
    {
        perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) 
    {
        perror("fcntl F_SETFL");
        return -1;
    }
    return 0;
}

static void update_timer_display() 
{
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "Timp ramas: %d secunde", app.remaining_time);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.progress_bar), app.remaining_time / 19.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app.progress_bar), time_str);
}

static void handle_timeout() 
{
    if (!app.waiting_for_response && !app.game_over) 
    {
        char timeout_msg[] = "TIMEOUT";
        write(app.socket_fd, timeout_msg, strlen(timeout_msg) + 1);
        app.waiting_for_response = TRUE;
    
        for (int i = 0; i < 4; i++) 
        {
            gtk_widget_set_sensitive(app.answer_buttons[i], FALSE);
        }
    }
}
static gboolean timer_callback(gpointer data) 
{
    if (app.remaining_time > 0) 
    {
        app.remaining_time--;
        update_timer_display();
        
        if (app.remaining_time == 0) 
        {
            handle_timeout();
        }
        return G_SOURCE_CONTINUE;
    }
    return G_SOURCE_REMOVE;
}

static void start_timer() 
{
    app.remaining_time = 19;
    if (app.timer_id != 0) 
    {
        g_source_remove(app.timer_id);
    }
    app.timer_id = g_timeout_add(1000, timer_callback, NULL);
    update_timer_display();
}

static void send_answer(GtkWidget *button, gpointer user_data) 
{
    if (!app.waiting_for_response && !app.game_over) 
    {
        const char *answer = user_data;
        if (write(app.socket_fd, answer, strlen(answer) + 1) <= 0) 
        {
            gtk_label_set_text(GTK_LABEL(app.status_label), "Eroare la trimiterea raspunsului!");
            return;
        }

        app.waiting_for_response = TRUE;
        app.last_clicked_button = button;

        for (int i = 0; i < 4; i++) 
        {
            gtk_widget_set_sensitive(app.answer_buttons[i], FALSE);
        }

        if (app.timer_id != 0) 
        {
            g_source_remove(app.timer_id);
            app.timer_id = 0;
        }
    }
}
static void reset_button_colors() 
{
    for(int i = 0; i < 4; i++) 
    {
        GtkStyleContext *context = gtk_widget_get_style_context(app.answer_buttons[i]);
        gtk_style_context_remove_class(context, "correct");
        gtk_style_context_remove_class(context, "wrong");
    }
}
static void quit_game() 
{
    if (app.socket_fd != -1) 
    {
        if (!app.game_over) 
        {
            char quit_msg[] = "quit";
            write(app.socket_fd, quit_msg, strlen(quit_msg) + 1);
        }
        close(app.socket_fd);
        app.socket_fd = -1;
    }
    gtk_main_quit();
}
static gboolean process_server_message(const char *buffer) 
{

    printf("[DEBUG] Procesare mesaj: %s\n", buffer);
    if (strstr(buffer, "LEADERBOARD PENTRU PARTIDA:") != NULL) 
    {
        printf("[DEBUG] Am primit leaderboard-ul\n");
        app.game_over = TRUE;
        char formatted_message[8192];
        snprintf(formatted_message, sizeof(formatted_message),"\n===== REZULTATE FINALE =====\n\n%s", buffer);
        
        gtk_label_set_text(GTK_LABEL(app.question_label), formatted_message);
        gtk_label_set_text(GTK_LABEL(app.status_label), "Joc terminat!");
        
        gtk_widget_hide(app.progress_bar);
        GtkWidget *answers_box = gtk_widget_get_parent(app.answer_buttons[0]);
        gtk_widget_hide(answers_box);
        gtk_widget_hide(app.quit_button);
        gtk_widget_show(app.close_button);
        if (app.timer_id != 0) 
        {
            g_source_remove(app.timer_id);
            app.timer_id = 0;
        }
        
        return TRUE;
    }
    if (strstr(buffer, "GAME OVER") != NULL) 
    {
        printf("[DEBUG] Am primit GAME OVER\n");
        app.game_over = TRUE;
        close(app.socket_fd);
        app.socket_fd = -1;
        return TRUE;
    }
    if (strstr(buffer, "[") != NULL && strstr(buffer, "A.") != NULL && strstr(buffer, "B.") != NULL) 
    {
        printf("[DEBUG] Noua intrebare - resetez tot\n");
        app.response_order = 0;
        
        if (app.timer_id != 0) 
        {
            g_source_remove(app.timer_id);
            app.timer_id = 0;
        }
        gtk_widget_show(app.progress_bar);
        GtkWidget *answers_box = gtk_widget_get_parent(app.answer_buttons[0]);
        gtk_widget_show(answers_box);
        gtk_widget_show(app.quit_button);
        gtk_widget_hide(app.close_button); 

        for(int i = 0; i < 4; i++) 
        {
            GtkStyleContext *context = gtk_widget_get_style_context(app.answer_buttons[i]);
            gtk_style_context_remove_class(context, "correct");
            gtk_style_context_remove_class(context, "wrong");
            gtk_widget_set_sensitive(app.answer_buttons[i], TRUE);
        }
        
        gtk_label_set_text(GTK_LABEL(app.question_label), buffer);
        gtk_label_set_text(GTK_LABEL(app.status_label), "");
        
        app.waiting_for_response = FALSE;
        app.last_clicked_button = NULL;
        
        start_timer();
        return TRUE;
    }
    if (strstr(buffer, "Raspuns corect") != NULL || strstr(buffer, "Raspuns gresit") != NULL) 
    {
        app.response_order++;
        
        bool is_correct = strstr(buffer, "Raspuns corect") != NULL;
        char correct_answer = is_correct ? '\0' : buffer[strlen(buffer) - 2];

        for(int i = 0; i < 4; i++) 
        {
            GtkStyleContext *context = gtk_widget_get_style_context(app.answer_buttons[i]);
            if(app.answer_buttons[i] == app.last_clicked_button) 
            {
                gtk_style_context_add_class(context, is_correct ? "correct" : "wrong");
            }
            if(!is_correct && gtk_button_get_label(GTK_BUTTON(app.answer_buttons[i]))[0] == correct_answer) 
            {
                gtk_style_context_add_class(context, "correct");
            }
        }

        if (app.response_order == 3) 
        {
            g_timeout_add(500, (GSourceFunc)reset_button_colors, NULL);
        }

        app.waiting_for_response = FALSE;
        return TRUE;
    }

    if (strstr(buffer, "Timpul a expirat") != NULL) 
    {
        app.response_order++;
        char correct_answer = buffer[strlen(buffer) - 2];
        
        for(int i = 0; i < 4; i++) 
        {
            if(gtk_button_get_label(GTK_BUTTON(app.answer_buttons[i]))[0] == correct_answer) 
            {
                GtkStyleContext *context = gtk_widget_get_style_context(app.answer_buttons[i]);
                gtk_style_context_add_class(context, "correct");
            }
        }

        if (app.response_order == 3) 
        {
            g_timeout_add(500, (GSourceFunc)reset_button_colors, NULL);
        }

        app.waiting_for_response = FALSE;
        return TRUE;
    }
    if (strstr(buffer, "Nu mai sunt sufiecienti jucatori") != NULL) 
    {
        app.game_over = TRUE;
        gtk_label_set_text(GTK_LABEL(app.question_label), buffer);
        gtk_label_set_text(GTK_LABEL(app.status_label), "Joc terminat prematur!");
        
        for (int i = 0; i < 4; i++) 
        {
            gtk_widget_set_sensitive(app.answer_buttons[i], FALSE);
        }
        if (app.timer_id != 0) 
        {
            g_source_remove(app.timer_id);
            app.timer_id = 0;
        }
        return TRUE;
    }
    gtk_label_set_text(GTK_LABEL(app.status_label), buffer);
    return TRUE;
}
static gboolean receive_data(GIOChannel *source, GIOCondition condition, gpointer data) 
{
    static char partial_buffer[8192] = {0};
    static size_t partial_len = 0;
    char buffer[4096];
    gsize bytes_read;
    GError *error = NULL;
    
    if (condition & G_IO_HUP) 
    {
        printf("[DEBUG] Conexiune inchisa de server\n");
        gtk_label_set_text(GTK_LABEL(app.status_label), "Conexiunea cu serverul a fost inchisa");
        return FALSE;
    }
    
    GIOStatus status = g_io_channel_read_chars(source, buffer, sizeof(buffer) - 1, &bytes_read, &error);
    
    if (status == G_IO_STATUS_ERROR) 
    {
        printf("[DEBUG] Eroare la citire: %s\n", error->message);
        gtk_label_set_text(GTK_LABEL(app.status_label), "Eroare la citirea datelor de la server");
        return FALSE;
    }
    
    if (bytes_read > 0) 
    {
        buffer[bytes_read] = '\0';
        
        if (partial_len + bytes_read >= sizeof(partial_buffer)) 
        {
            printf("[DEBUG] Reset buffer din cauza overflow\n");
            partial_len = 0;
        }
        memcpy(partial_buffer + partial_len, buffer, bytes_read);
        partial_len += bytes_read;
        
        char *current = partial_buffer;
        char *end;
        while ((end = memchr(current, '\0', partial_buffer + partial_len - current)) != NULL) 
        {
            printf("[DEBUG] Procesare mesaj complet: %.100s\n", current);
            if (process_server_message(current) && strstr(current, "GAME OVER") != NULL) 
            {
                printf("[DEBUG] Joc terminat, inchidere conexiune\n");
                return FALSE;
            }
            current = end + 1;
            if (current >= partial_buffer + partial_len) 
            {
                break;
            }
        }
        if (current < partial_buffer + partial_len) 
        {
            size_t remaining = partial_buffer + partial_len - current;
            memmove(partial_buffer, current, remaining);
            partial_len = remaining;
        } 
        else 
        {
            partial_len = 0;
        }
    }
    
    return TRUE;
}

static void connect_to_server(GtkWidget *button, gpointer data) 
{
    const char *nickname = gtk_entry_get_text(GTK_ENTRY(app.nickname_entry));
    if (strlen(nickname) == 0) 
    {
        gtk_label_set_text(GTK_LABEL(app.status_label), "Va rugam introduceti un nickname!");
        return;
    }

    struct sockaddr_in server;
    app.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (app.socket_fd == -1) 
    {
        gtk_label_set_text(GTK_LABEL(app.status_label), "Eroare la crearea socketului!");
        return;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(2024);

    if (connect(app.socket_fd, (struct sockaddr *)&server,sizeof(struct sockaddr)) == -1) 
    {
        gtk_label_set_text(GTK_LABEL(app.status_label), "Eroare la conectarea la server!");
        close(app.socket_fd);
        return;
    }

    char welcome_msg[4096];
    int total_bytes = 0;
    while (total_bytes < sizeof(welcome_msg) - 1) 
    {
        int bytes_read = read(app.socket_fd, welcome_msg + total_bytes,sizeof(welcome_msg) - 1 - total_bytes);
        if (bytes_read < 0) 
        {
            if (errno == EINTR) continue;
            gtk_label_set_text(GTK_LABEL(app.status_label), "Eroare la citirea mesajului de bun venit!");
            close(app.socket_fd);
            return;
        }
        if (bytes_read == 0) break;
        total_bytes += bytes_read;
        if (welcome_msg[total_bytes-1] == '\0') break; 
    }
    welcome_msg[total_bytes] = '\0';
    gtk_label_set_text(GTK_LABEL(app.question_label), welcome_msg);

    set_socket_nonblocking(app.socket_fd);

    if (write(app.socket_fd, nickname, strlen(nickname) + 1) <= 0) 
    {
        gtk_label_set_text(GTK_LABEL(app.status_label),"Eroare la trimiterea nickname-ului!");
        close(app.socket_fd);
        return;
    }

    gtk_widget_set_sensitive(app.nickname_entry, FALSE);
    gtk_widget_set_sensitive(app.connect_button, FALSE);

    GIOChannel *channel = g_io_channel_unix_new(app.socket_fd);
    g_io_channel_set_encoding(channel, NULL, NULL);
    g_io_channel_set_buffered(channel, FALSE);
    g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);
    g_io_add_watch(channel, G_IO_IN | G_IO_HUP, receive_data, NULL);
    g_io_channel_unref(channel);

    gtk_label_set_text(GTK_LABEL(app.status_label), "Conectat! Asteptam inceperea jocului...");
}
static void apply_css(void) 
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window {"
        "   background-color: #E6E6FA;"
        "}"
        "button {"
        "   background-color: #9370DB;"
        "   color: white;"
        "   border-radius: 2px;"
        "   padding: 2px 4px;"
        "   margin: 1px;"
        "   transition: background-color 200ms ease-in-out;"
        "}"
        "button:hover {"
        "   background-color: #8A2BE2;"
        "}"
        "button.connect-button {"
        "   background-color: #4CAF50;" 
        "}"
        "button.connect-button:hover {"
        "   background-color: #45a049;"  
        "}"
        "button.quit-button {"
        "   background-color: #F44336;"
        "}"
        "button.quit-button:hover {"
        "   background-color: #d32f2f;"
        "}"
        "button.correct {"
        "   background-color: #4CAF50;"
        "}"
        "button.wrong {"
        "   background-color: #F44336;"
        "}"
        ".question-text {"
        "   font-size: 11px;"         
        "   font-weight: bold;"       
        "   padding: 1px;"
        "   margin: 1px;"
        "   color: #2C3E50;"      
        "}"
        ".welcome-text {"
        "   font-size: 13px;"         
        "   font-weight: bold;"       
        "   padding: 1px;"
        "   margin: 1px;"
        "   color: #2C3E50;"      
        "}"
        ".motivational-text {"
        "   font-size: 12px;"         
        "   font-weight: bold;"       
        "   padding: 2px;"
        "   margin: 2px;"
        "   color: #2C3E50;"      
        "}", -1, NULL);
    
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}
int main(int argc, char *argv[]) 
{
    gtk_init(&argc, &argv);
    apply_css();

    app.socket_fd = -1;
    app.timer_id = 0;
    app.waiting_for_response = FALSE;
    app.game_over = FALSE;

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Quizz Game");
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 3);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 400, 300);
    g_signal_connect(app.window, "destroy", G_CALLBACK(quit_game), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);
    GtkWidget *title_label = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<span font='20' weight='bold'>🎮 Quizz Game 🎮</span>");
    gtk_label_set_markup(GTK_LABEL(title_label), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 5);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, FALSE, FALSE, 3);


    GtkWidget *motivational_label = gtk_label_new(NULL);
    char *motivational_markup = g_markup_printf_escaped("<span>✨ Testeaza-ti cunostintele de cultura generala! 🎯</span>");
    gtk_label_set_markup(GTK_LABEL(motivational_label), motivational_markup);
    g_free(motivational_markup);
    gtk_style_context_add_class(gtk_widget_get_style_context(motivational_label), "motivational-text");
    gtk_box_pack_start(GTK_BOX(vbox), motivational_label, FALSE, FALSE, 3);

    GtkWidget *connect_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), connect_box, FALSE, FALSE, 3);

    app.nickname_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.nickname_entry), "Introduceti nickname");
    gtk_box_pack_start(GTK_BOX(connect_box), app.nickname_entry, TRUE, TRUE, 0);

    app.connect_button = gtk_button_new_with_label("Conectare");
    GtkStyleContext *connect_context = gtk_widget_get_style_context(app.connect_button);
    gtk_style_context_add_class(connect_context, "connect-button");
    g_signal_connect(app.connect_button, "clicked", G_CALLBACK(connect_to_server), NULL);
    gtk_box_pack_start(GTK_BOX(connect_box), app.connect_button, FALSE, FALSE, 0);

    GtkWidget *small_spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox), small_spacer, FALSE,FALSE,2);

    app.progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app.progress_bar), "Timp ramas: -");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app.progress_bar), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(app.progress_bar), -1, 10);
    gtk_box_pack_start(GTK_BOX(vbox), app.progress_bar, FALSE, FALSE, 2);
    gtk_widget_hide(app.progress_bar); 


    app.question_label = gtk_label_new("Conectati-va pentru a incepe jocul...");
    gtk_label_set_line_wrap(GTK_LABEL(app.question_label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(app.question_label), PANGO_WRAP_WORD);
    gtk_label_set_max_width_chars(GTK_LABEL(app.question_label), 35);
    gtk_label_set_lines(GTK_LABEL(app.question_label), 3);
    gtk_widget_set_size_request(GTK_WIDGET(app.question_label), -1, 35);
    GtkStyleContext *context = gtk_widget_get_style_context(app.question_label);
    gtk_style_context_add_class(context, "question-text");
    gtk_box_pack_start(GTK_BOX(vbox), app.question_label, FALSE, FALSE, 2);

    const char *answers[] = {"A", "B", "C", "D"};
    GtkWidget *answers_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), answers_box, FALSE, FALSE, 0);
    gtk_widget_hide(answers_box); 

    for (int i = 0; i < 4; i++) 
    {
        app.answer_buttons[i] = gtk_button_new_with_label(answers[i]);
        gtk_widget_set_sensitive(app.answer_buttons[i], FALSE);
        g_signal_connect(app.answer_buttons[i], "clicked", G_CALLBACK(send_answer), (gpointer)answers[i]);
        gtk_box_pack_start(GTK_BOX(answers_box), app.answer_buttons[i], TRUE, TRUE, 0);
    }

    app.status_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(app.status_label), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), app.status_label, FALSE, FALSE, 5);

    app.quit_button = gtk_button_new_with_label("Iesire");
    GtkStyleContext *quit_context = gtk_widget_get_style_context(app.quit_button);
    gtk_style_context_add_class(quit_context, "quit-button");
    g_signal_connect(app.quit_button, "clicked", G_CALLBACK(quit_game), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), app.quit_button, FALSE, FALSE, 0);

    app.close_button = gtk_button_new_with_label("Inchide Joc");
    g_signal_connect(app.close_button, "clicked", G_CALLBACK(quit_game), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), app.close_button, FALSE, FALSE, 0);

    gtk_widget_show_all(app.window);
    gtk_widget_hide(app.progress_bar);
    gtk_widget_hide(answers_box); 
    gtk_widget_hide(app.quit_button);

    gtk_main();
    if (app.timer_id != 0) 
    {
        g_source_remove(app.timer_id);
    }
    if (app.socket_fd != -1) 
    {
        close(app.socket_fd);
    }

    return 0;
}