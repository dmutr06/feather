#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include  <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "feather.h"
#include <uv.h>

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

    char *buf = malloc(128);
    const char *id = req->params[0].value;
    snprintf(buf, 128, "<h2>User Profile: %s</h2>", id ? id : "(null)");
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

typedef struct {
  uv_tcp_t handle;
  uv_write_t write_req;
  char rbuf[1024];
  char wbuf[1024];
  size_t len;
  FeatherApp *app;
} client_t;

void alloc_buf(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  client_t *client = (client_t *) handle->data;
  *buf = uv_buf_init(client->rbuf, sizeof(client->rbuf));
}

void write_cb(uv_write_t *req, int status) {
  client_t *client = (client_t *) req->handle->data;

  // uv_close((uv_handle_t *) &client->handle, NULL);

  // free(client);
}

void read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  client_t *client = (client_t *) stream->data;

  if (nread < 0) {
    uv_close((uv_handle_t *) stream, NULL);
    free(client);
    return;
  }

  printf("%*s", nread, client->rbuf);

  client->len = nread;
  client->rbuf[nread] = '\0';

  FeatherRequest req = {0};
  feather_parse_request(&req, client->rbuf);
  
  FeatherResponse res = {0};

  FeatherHandler handler = feather_find_handler(client->app, &req);
  if (handler) {
      handler(&req, &res);
  } else {
      res.status = 404;
      feather_response_set_body(&res, "Not Found");
  }

  size_t res_len = feather_dump_response(&res, client->wbuf, sizeof(client->wbuf));
  printf("%*s", res_len, client->wbuf);
  uv_buf_t wrbuf = uv_buf_init(client->wbuf, res_len);
  uv_write(&client->write_req, (uv_stream_t *) &client->handle, &wrbuf, 1, write_cb);
}

void on_new_conn(uv_stream_t *server, int status) {
  if (status < 0) return;

  client_t *client = malloc(sizeof(client_t));
  uv_tcp_init(server->loop, &client->handle);
  client->handle.data = client;
  client->app = server->data;

  if (uv_accept(server, (uv_stream_t *) &client->handle) == 0) {
    uv_read_start((uv_stream_t *) &client->handle, alloc_buf, read_cb);
  } else {
    uv_close((uv_handle_t *) &client->handle, NULL);
    free(client);
  }
}

int main() {
    FeatherApp app;
    feather_init_app(&app);

    feather_get(&app, "/home", home_handler);
    feather_get(&app, "/about", about_handler);
    feather_get(&app, "/user", user_handler);
    feather_get(&app, "/user/:id", user_id_handler);


    uv_loop_t *loop = uv_default_loop();
    uv_tcp_t server;
    uv_tcp_init(loop, &server);
    server.data = &app;

    struct sockaddr_in addr;
    uv_ip4_addr("localhost", 6969, &addr);

    uv_tcp_bind(&server, (const struct sockaddr *) &addr, 0);
    int res = uv_listen((uv_stream_t *) &server, 128, on_new_conn);

    if (res) {
      fprintf(stderr, "Listen error: %s\n", uv_strerror(res));
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}
