#include <stdio.h>
#include <stdlib.h>

#include "pixelflut.h"

#define PANIC(msg) do { fprintf(stderr, "%s\n", msg); exit(EXIT_FAILURE); } while (0)
#define ASSERT(cond, msg) do { if (!(cond)) PANIC(msg); } while (0)
#define PF_ASSERT(expr, msg) do { \
    if ((res = (expr))) { \
        fprintf(stderr, "%s: %s\n", msg, pf_error_msg(res)); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

int main(int argc, char *argv[]) {
    ASSERT(argc >= 3, "arguments: <address> <port>");
    struct pf_conn conn;
    enum pf_result res;
    PF_ASSERT(pf_connect_raw(argv[1], argv[2], &conn), "could not establish connection");
    uint16_t width, height;
    PF_ASSERT(pf_get_size(&conn, &width, &height), "could not get size");
    printf("got SIZE %d %d\n", width, height);

    // note that this is slow, because it doesn't use buffering (see `pf_*_many`)
    struct pixel px = { 0 };
    for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
            px.x = x;
            px.y = y;
            PF_ASSERT(pf_get(&conn, &px), "could not get pixel");
            // invert color
            px.r = ~px.r;
            px.g = ~px.g;
            px.b = ~px.b;
            PF_ASSERT(pf_put_rgb(&conn, px), "could not put pixel");
        }
        printf("finished line %d\n", y);
    }
    pf_disconnect(&conn);
}
