#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>

#include "pixelflut.h"

#define DO_CLOSE(conn) do {     \
    if ((conn)->sockfd != -1) { \
        close((conn)->sockfd);  \
        (conn)->sockfd = -1;    \
    }                           \
} while (0)

enum pf_result
pf_connect_raw(char *addr, char *port, struct pf_conn *conn) {
    if (addr == NULL || port == NULL || conn == NULL) {
        return PF_NULL_ARG;
    }
    // 0-initialize all members
    memset(conn, 0, sizeof(*conn));
    conn->sockfd = -1;
    enum pf_result res = PF_OK;

    struct in_addr ip_addr;
    if (!inet_aton(addr, &ip_addr)) {
        res = PF_CONNECT_PARSE_ADDR;
        goto fail;
    }

    errno = 0;    /* To distinguish success/failure after call */
    char *endptr = NULL;
    unsigned long parsed_port = strtoul(port, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || parsed_port > 0xffff) {
        res = PF_CONNECT_PARSE_PORT;
        goto fail;
    }

    conn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sockfd == -1) {
        res = PF_SYS_SOCKET;
        goto fail;
    }

    struct sockaddr_in sock_addr = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)parsed_port),
        .sin_addr = ip_addr
    };

    int status = connect(conn->sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (status == -1) {
        res = PF_SYS_CONNECT;
        goto fail;
    }

    return PF_OK;
    
fail:
    DO_CLOSE(conn);
    return res;
}

void
pf_disconnect(struct pf_conn *conn) {
    if (conn != NULL) {
        DO_CLOSE(conn);
    }
}

// #define MONITOR_SYSCALLS

static ssize_t
do_write_single(int fd, char *buf, size_t len) {
    ssize_t status = write(fd, buf, len);
#ifdef MONITOR_SYSCALLS
    if (status == -1) {
        perror("MONITOR: failed write syscall");
    } else {
        fprintf(stderr, "MONITOR: successful write syscall (%zu bytes)\n%.*s\nEND MONITOR\n", (size_t)status, (int)status, buf);
    }
#endif
    return status;
}

static ssize_t
do_read_single(int fd, char *buf, size_t len) {
    ssize_t status = read(fd, buf, len);
#ifdef MONITOR_SYSCALLS
    if (status == -1) {
        perror("MONITOR: failed read syscall");
    } else {
        fprintf(stderr, "MONITOR: successful read syscall (%zu bytes)\n%.*s\nEND MONITOR\n", (size_t)status, (int)status, buf);
    }
#endif
    return status;
}

#define CONN_VALID(conn) ((conn) != NULL && (conn)->sockfd != -1)

static enum pf_result
write_all(int fd, char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t status = do_write_single(fd, buf + written, len - written);
        if (status == -1) {
            return PF_SYS_WRITE;
        } else if (status == 0) {
            return PF_SYS_WRITE_RETURNED_ZERO;
        }
        written += (size_t)status;
    }
    return PF_OK;
}

static enum pf_result
pf_put_general(struct pf_conn *conn, struct pixel px, bool use_alpha) {
    if (!CONN_VALID(conn)) {
        return PF_CONN_INVALID_STATE;
    }
    enum pf_result res = PF_OK;
    char buf[32];
    int len;
    // TODO no error handling here, should we do it?
    if (use_alpha) {
        len = sprintf(buf, "PX %d %d %02x%02x%02x%02x\n", px.x, px.y, px.r, px.g, px.b, px.a);
    } else {
        len = sprintf(buf, "PX %d %d %02x%02x%02x\n", px.x, px.y, px.r, px.g, px.b);
    }

    if ((res = write_all(conn->sockfd, buf, len)) != PF_OK) {
        goto fail;
    }
    conn->num_pixels_written++;
    return PF_OK;

fail:
    DO_CLOSE(conn);
    return res;
}

enum pf_result
pf_put_rgb(struct pf_conn *conn, struct pixel px) {
    return pf_put_general(conn, px, false);
}

enum pf_result
pf_put_rgba(struct pf_conn *conn, struct pixel px) {
    return pf_put_general(conn, px, true);
}

struct pf_buf {
    size_t len;
    size_t cap;
    size_t read_pos;
    char *data;
};

#define BUF_VALID(buf) ((buf) != NULL \
    && (buf)->cap >= PF_MIN_BUFFER_SIZE \
    && (buf)->len <= (buf)->cap \
    && (buf)->read_pos <= (buf)->len \
    && (buf)->data != NULL)

static
enum pf_result
line_advance(struct pf_conn *conn, struct pf_buf *buf, char **line_start) {
#ifdef PF_BUG_CATCHING
    if (!CONN_VALID(conn) || !BUF_VALID(buf) || line_start== NULL) {
        return PF_BUG;
    }
#endif
    size_t num_bytes_searched = 0;
    while (1) {
        // TODO could use memchr
        for (size_t i = buf->read_pos + num_bytes_searched; i < buf->len; i++) {
            if (buf->data[i] == '\n') {
                *line_start = buf->data + buf->read_pos;
                buf->read_pos = i + 1;
                return PF_OK;
            }
        }
        num_bytes_searched = buf->len - buf->read_pos;
        if (num_bytes_searched > PF_MIN_BUFFER_SIZE) {
            // something went wrong. We went too long without receiving a newline.
            return PF_PROTOCOL_ERROR;
        }

        // move buffer to front
        memmove(buf->data, buf->data + buf->read_pos, buf->len - buf->read_pos);
        buf->len -= buf->read_pos;
        buf->read_pos = 0;

        ssize_t status = do_read_single(conn->sockfd, buf->data + buf->len, buf->cap - buf->len);
        if (status == -1) {
            return PF_SYS_READ;
        } else if (status == 0) {
            return PF_SYS_READ_RETURNED_ZERO;
        }
        buf->len += (size_t)status;
    }
}

#define BUFFER_HAS_UNREAD_BYTES(buf) ((buf)->read_pos < (buf)->len)

// read single line from server and make sure nothing more has been received.
static enum pf_result
get_single_line(struct pf_conn *conn, char *buf) {
    enum pf_result res = PF_OK;
    struct pf_buf real_buf = {
        .len = 0,
        .cap = PF_MIN_BUFFER_SIZE,
        .read_pos = 0,
        .data = buf
    };
    char *line_start;
    if ((res = line_advance(conn, &real_buf, &line_start)) != PF_OK) {
        return res;
    }
    if (BUFFER_HAS_UNREAD_BYTES(&real_buf)) {
        return PF_READ_TOO_MUCH;
    }
    return PF_OK;
}

enum pf_result
pf_get_size(struct pf_conn *conn, uint16_t *width, uint16_t *height) {
    if (!CONN_VALID(conn)) {
        return PF_CONN_INVALID_STATE;
    }
    // No NULL checking here because it's allowed (see header)
    enum pf_result res = PF_OK;
    if ((res = write_all(conn->sockfd, "SIZE\n", 5))) {
        goto fail;
    }
    char buf[PF_MIN_BUFFER_SIZE];
    if ((res = get_single_line(conn, buf)) != PF_OK) {
        goto fail;
    }
    unsigned long w, h;
    int match = sscanf(buf, "SIZE %lu %lu\n", &w, &h);
    if (match != 2 || w > 0xffff || h > 0xffff) {
        res = PF_PROTOCOL_ERROR;
        goto fail;
    }
    if (width != NULL) {
        *width = (uint16_t)w;
    }
    if (height != NULL) {
        *height = (uint16_t)h;
    }

    return PF_OK;

fail:
    DO_CLOSE(conn);
    return res;
}

enum pf_result
pf_get(struct pf_conn *conn, struct pixel *px) {
    if (!CONN_VALID(conn)) {
        return PF_CONN_INVALID_STATE;
    }
    if (px == NULL) {
        return PF_NULL_ARG;
    }
    enum pf_result res = PF_OK;
    char buf[PF_MIN_BUFFER_SIZE];
    int len = sprintf(buf, "PX %d %d\n", px->x, px->y);
    if ((res = write_all(conn->sockfd, buf, len)) != PF_OK) {
        goto fail;
    }
    if ((res = get_single_line(conn, buf)) != PF_OK) {
        goto fail;
    }
    unsigned int server_x, server_y;
    unsigned int r, g, b;
    int match = sscanf(buf, "PX %u %u %02x%02x%02x\n", &server_x, &server_y, &r, &g, &b);
    if (match != 5 || server_x > 0xffff || server_y > 0xff) {
        res = PF_PROTOCOL_ERROR;
        goto fail;
    }
    if (server_x != px->x || server_y != px->y) {
        res = PF_GET_UNEXPECTED_COORDS;
        goto fail;
    }
    px->r = (uint8_t)r;
    px->g = (uint8_t)g;
    px->b = (uint8_t)b;
    px->a = 0xff;
    conn->num_pixels_read++;

    return PF_OK;

fail:
    DO_CLOSE(conn);
    return res;
}

const char *pf_error_msg(enum pf_result res) {
    switch (res) {
        case PF_OK: return "OK";
        case PF_CONN_INVALID_STATE: return "called function with failed/closed connection";
        case PF_NULL_ARG: return "encountered NULL argument";
        case PF_CONNECT_PARSE_ADDR: return "could not parse address";
        case PF_CONNECT_PARSE_PORT: return "could not parse port";
        case PF_SYS_SOCKET: return "could not create socket";
        case PF_SYS_CONNECT: return "could not connect socket";
        case PF_SYS_WRITE: return "write() failed";
        case PF_SYS_READ: return "read() failed";
        case PF_READ_TOO_MUCH: return "read more lines from the server than expected";
        case PF_SYS_WRITE_RETURNED_ZERO: return "write() returned 0 -- closed connection?";
        case PF_SYS_READ_RETURNED_ZERO: return "read() returned 0 -- closed connection?";
        case PF_PROTOCOL_ERROR: return "server sent an invalid response";
        case PF_GET_UNEXPECTED_COORDS: return "got pixel with unexpected coords from server";
        case PF_BUFFER_SIZE: return "buffer too small";
#ifdef PF_BUG_CATCHING
        case PF_BUG: return "bug in internal library function!";
#endif
    }
    return "?";
}

// flushes a buffer and sets its length to 0.
static enum pf_result
do_flush(struct pf_conn *conn, struct pf_buf *buf) {
#ifdef PF_BUG_CATCHING
    if (!CONN_VALID(conn) || !BUF_VALID(buf)) {
        return PF_BUG;
    }
#endif
    enum pf_result res;
    if ((res = write_all(conn->sockfd, buf->data, buf->len)) != PF_OK) {
        return res;
    }
    buf->len = 0;
    return PF_OK;
}

static enum pf_result
add_to_buffer(struct pf_conn *conn, struct pf_buf *buf, char *data, size_t n) {
#ifdef PF_BUG_CATCHING
    if (!CONN_VALID(conn) || !BUF_VALID(buf) || data == NULL || n == 0 || buf->cap < n) {
        return PF_BUG;
    }
#endif
    enum pf_result res;
    if (buf->cap - buf->len < n) {
        if ((res = do_flush(conn, buf)) != PF_OK) {
            return res;
        }
    }
    memcpy(buf->data + buf->len, data, n);
    buf->len += n;
    return PF_OK;
}

static
enum pf_result
pf_put_general_many(struct pf_conn *conn, struct pixel *pxs, size_t n, bool use_alpha,
    char *buf, size_t buf_size)
{
    if (!CONN_VALID(conn)) {
        return PF_CONN_INVALID_STATE;
    }
    if (pxs == NULL || buf == NULL) {
        return PF_NULL_ARG;
    }
    if (buf_size < PF_MIN_BUFFER_SIZE) {
        return PF_BUFFER_SIZE;
    }
    struct pf_buf real_buf = {
        .len = 0,
        .cap = buf_size,
        .read_pos = 0,
        .data = buf
    };
    enum pf_result res = PF_OK;
    char printbuf[32];
    for (size_t i = 0; i < n; i++) {
        struct pixel px = pxs[i];
        int len;
        if (use_alpha) {
            len = sprintf(printbuf, "PX %d %d %02x%02x%02x%02x\n", px.x, px.y, px.r, px.g, px.b, px.a);
        } else {
            len = sprintf(printbuf, "PX %d %d %02x%02x%02x\n", px.x, px.y, px.r, px.g, px.b);
        }
        if ((res = add_to_buffer(conn, &real_buf, printbuf, len)) != PF_OK) {
            goto fail;
        }
    }
    if ((res = do_flush(conn, &real_buf)) != PF_OK) {
        goto fail;
    }
    conn->num_pixels_written += n;
    return PF_OK;
fail:
    DO_CLOSE(conn);
    return res;
}

enum pf_result
pf_put_rgb_many(struct pf_conn *conn, struct pixel *pxs, size_t n,
    char *buf, size_t buf_size)
{
    return pf_put_general_many(conn, pxs, n, false, buf, buf_size);
}

enum pf_result
pf_put_rgba_many(struct pf_conn *conn, struct pixel *pxs, size_t n,
    char *buf, size_t buf_size)
{
    return pf_put_general_many(conn, pxs, n, true, buf, buf_size);
}

// receives the n pixels from `pxs[0]` to `pxs[n-1]`.
// checks that the server only sent this response
static enum pf_result
pf_get_many_recv(struct pf_conn *conn, struct pixel *pxs, size_t n,
    char *buf, size_t buf_size)
{
#ifdef PF_BUG_CATCHING
    if (!CONN_VALID(conn) || pxs == 0 || n == 0 || buf == NULL || buf_size < PF_MIN_BUFFER_SIZE) {
        return PF_BUG;
    }
#endif
    enum pf_result res;
    struct pf_buf real_buf = {
        .len = 0,
        .cap = buf_size,
        .data = buf
    };
    for (size_t i = 0; i < n; i++) {
        char *line_start = NULL;
        if ((res = line_advance(conn, &real_buf, &line_start)) != PF_OK) {
            return res;
        }
        unsigned int server_x, server_y;
        unsigned int r, g, b;
        int match = sscanf(line_start, "PX %u %u %02x%02x%02x\n", &server_x, &server_y, &r, &g, &b);
        if (match != 5 || server_x > 0xffff || server_y > 0xffff) {
            return PF_PROTOCOL_ERROR;
        }
        if (server_x != pxs[i].x || server_y != pxs[i].y) {
            return PF_GET_UNEXPECTED_COORDS;
        }

        pxs[i].r = r;
        pxs[i].g = g;
        pxs[i].b = b;
        pxs[i].a = 0xff;
    }
    if (BUFFER_HAS_UNREAD_BYTES(&real_buf)) {
        return PF_READ_TOO_MUCH;
    }
    return PF_OK;
}

enum pf_result
pf_get_many(struct pf_conn *conn, struct pixel *pxs, size_t n,
    char *buf, size_t buf_size,
    size_t batch_limit)
{
    if (!CONN_VALID(conn)) {
        return PF_CONN_INVALID_STATE;
    }
    if (pxs == NULL || buf == NULL) {
        return PF_NULL_ARG;
    }
    if (buf_size < PF_MIN_BUFFER_SIZE) {
        return PF_BUFFER_SIZE;
    }
    struct pf_buf real_buf = {
        .len = 0,
        .cap = buf_size,
        .read_pos = 0,
        .data = buf
    };
    enum pf_result res = PF_OK;
    char printbuf[32];

    size_t idx = 0;
    size_t curr_batch_start = 0;
    while (idx < n) {
        int len = sprintf(printbuf, "PX %d %d\n", pxs[idx].x, pxs[idx].y);
        if ((res = add_to_buffer(conn, &real_buf, printbuf, len)) != PF_OK) {
            goto fail;
        }
        idx++;
        if (batch_limit > 0 && idx == curr_batch_start + batch_limit) {
            if ((res = do_flush(conn, &real_buf)) != PF_OK) {
                goto fail;
            }
            if ((res = pf_get_many_recv(conn, pxs + curr_batch_start, batch_limit, buf, buf_size))) {
                goto fail;
            }
            curr_batch_start = idx;
        }
    }
    if (idx > curr_batch_start) {
        if ((res = do_flush(conn, &real_buf)) != PF_OK) {
            goto fail;
        }
        if ((res = pf_get_many_recv(conn, pxs + curr_batch_start, idx - curr_batch_start, buf, buf_size))) {
            goto fail;
        }
    }
    conn->num_pixels_read += n;
    return PF_OK;

fail:
    DO_CLOSE(conn);
    return res;
}
