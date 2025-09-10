#include "feather.h"
#include <stdio.h>

void home_handler(const FeatherRequest *req, FeatherCtx *ctx) {
    (void) req;

    FeatherResponse res = {0};
    res.status = 200;
    feather_response_set_body(&res, "Hello, World!");

    feather_response_send(ctx, &res);
}

void about_handler(const FeatherRequest *req, FeatherCtx *ctx) {
    (void) req;

    FeatherResponse res = {0};
    res.status = 200;
    feather_response_set_body(&res, "<h2>About page</h2>");
    feather_response_set_header(&res, "Content-Type", "text/html");

    feather_response_send(ctx, &res);
}

int main() {
    FeatherApp app;
    feather_init_app(&app);

    feather_get(&app, "/home", home_handler);
    feather_get(&app, "/about", about_handler);

    int res = feather_run(&app, 6969);

    if (res != 0) {
        perror("feather_run");
        return 1;
    }

    return 0;
}
