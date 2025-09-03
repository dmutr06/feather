#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include  <unistd.h>
#include <arpa/inet.h>
#include "feather.h"

void home_handler(const FeatherRequest *req, FeatherResponse *res) {
    res->status = 200;
    feather_response_set_body(res, "<h1>Welcome to Home!</h1>");
    feather_response_set_header(res, "Content-Type", "text/html");
}

void about_handler(const FeatherRequest *req, FeatherResponse *res) {
    res->status = 200;
    feather_response_set_body(res, "<h1>About Page</h1>");
    feather_response_set_header(res, "Content-Type", "text/html");
}

void user_handler(const FeatherRequest *req, FeatherResponse *res) {
    res->status = 200;
    feather_response_set_body(res, "<h1>Users Index</h1>");
    feather_response_set_header(res, "Content-Type", "text/html");
}

void user_id_handler(const FeatherRequest *req, FeatherResponse *res) {
    res->status = 200;

    char buf[128];
    const char *id = req->params[0].value;
    snprintf(buf, sizeof(buf), "<h2>User Profile: %s</h2>", id ? id : "(null)");
    feather_response_set_body(res, buf);
    feather_response_set_header(res, "Content-Type", "text/html");
}

int feather_listen(const char *host, int port, FeatherApp *app) {
    int sfd, cfd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        return -1;
    }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host);
    addr.sin_port = htons(port);

    if (bind(sfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sfd);
        return -1;
    }

    if (listen(sfd, 69) < 0) {
        perror("listen failed");
        close(sfd);
        return -1;
    }

    feather_log("Feather HTTP server running on %s:%d", host, port);

    while (1) {
        cfd = accept(sfd, (struct sockaddr *) &addr, &addrlen);
        if (cfd < 0) {
            perror("accept failed");
            continue;
        }

        char buf[4096];
        int read_bytes = recv(cfd, buf, sizeof(buf) - 1, 0);
        if (read_bytes <= 0) {
            close(cfd);
            continue;
        }

        buf[read_bytes] = '\0';

        FeatherRequest req = {0};
        feather_parse_request(&req, buf);

        // feather_log_request(&req);

        FeatherResponse res = {0};

        FeatherHandler handler = feather_find_handler(app, &req);
        if (handler) {
            handler(&req, &res);
        } else {
            res.status = 404;
            feather_response_set_body(&res, "Not Found");
        }

        char res_buf[4096];
        size_t res_len = feather_dump_response(&res, res_buf, sizeof(res_buf));
        send(cfd, res_buf, res_len, 0);

        close(cfd);
    }

    close(cfd);
    return 0;
}

int main() {
    FeatherApp app;
    feather_init_app(&app);

    feather_get(&app, "/home", home_handler);
    feather_get(&app, "/about", about_handler);
    feather_get(&app, "/user", user_handler);
    feather_get(&app, "/user/:id", user_id_handler);

    feather_listen("0.0.0.0", 6969, &app);

    return 0;
}
