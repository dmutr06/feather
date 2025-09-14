#include "feather.h"
#include "strview.h"
#include <stdio.h>

void home_handler(const FeatherRequest *req, FeatherCtx *ctx) {
    (void) req;

    FeatherResponse res = {0};
    res.status = 200;
    res.body = SV_LIT(
        "<!DOCTYPE html>"
        "<html>"
            "<head>"
                "<title>Feather</title>"
                "<link rel=\"stylesheet\" href=\"styles.css\" />"
            "<head>"
            "<body>"
                "<h1 class=\"title\">Feather Web Framework</h1>"
                "<div class=\"subtitle\">it's pretty cool</div>"
            "</body>"
        "</html>"
    );

    feather_response_send(ctx, &res);
}

void home_styles_handler(const FeatherRequest *req, FeatherCtx *ctx) {
    (void) req;

    FeatherResponse res = {0};
    res.status = 200;
    res.body = SV_LIT(
        "body {"
            "background-color: #000;"
            "color: #fff;"
        "}"
        ".title {"
            "text-align: center;"
            "font-size: 32px;"
        "}"
        ".subtitle {"
            "text-align: center;"
            "margin-top: 8px;"
            "font-size: 24px;"
        "}"
    );

    feather_response_send(ctx, &res);
}

void about_handler(const FeatherRequest *req, FeatherCtx *ctx) {
    (void) req;

    FeatherResponse res = {0};
    res.status = 200;
    res.body = SV_LIT("<h2>About page</h2>");
    res.headers.content_type = SV_LIT("text/html");

    feather_response_send(ctx, &res);
}

void user_handler(const FeatherRequest *req, FeatherCtx *ctx) {
    char body[256];
    FeatherResponse res = {0};

    snprintf(body, sizeof(body) - 1, "<h1>Hello, "SV_FMT"</h1>", SV_ARG(req->params[0].value));
    res.status = 200;
    res.body = sv_from_cstr(body);
    res.headers.content_type = SV_LIT("text/html");

    feather_response_send(ctx, &res);
}

void new_user_handler(const FeatherRequest *req, FeatherCtx *ctx) {
    FeatherResponse res = {0};
    
    printf(SV_FMT"\n", SV_ARG(req->body));

    res.status = 201;
    res.body = SV_LIT("Created");

    feather_response_send(ctx, &res);
}

int main() {
    FeatherApp app;
    feather_init_app(&app);

    feather_get(&app, "/home", home_handler);
    feather_get(&app, "/styles.css", home_styles_handler);
    feather_get(&app, "/about", about_handler);
    feather_get(&app, "/user/:id", user_handler);
    feather_post(&app, "/user", new_user_handler);

    int res = feather_run(&app, 6969);

    if (res != 0) {
        perror("feather_run");
        return 1;
    }

    return 0;
}
