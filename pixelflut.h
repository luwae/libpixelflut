#ifndef LIB_PIXELFLUT_H
#define LIB_PIXELFLUT_H

#include <stdint.h>
#include <stddef.h>

#define PF_BUG_CATCHING

// All functions return this type.
// The value `0` means success, all other values encode various errors.
enum pf_result {
    PF_OK = 0,

    // invalid arguments
    PF_CONN_INVALID_STATE,
    PF_NULL_ARG,

    // connection
    PF_CONNECT_PARSE_ADDR,
    PF_CONNECT_PARSE_PORT,
    PF_SYS_SOCKET,
    PF_SYS_CONNECT,

    // I/O
    PF_SYS_WRITE,
    PF_SYS_READ,
    PF_SYS_WRITE_RETURNED_ZERO,
    PF_SYS_READ_RETURNED_ZERO,

    // protocol
    PF_PROTOCOL_ERROR,
    PF_READ_TOO_MUCH,
    PF_GET_UNEXPECTED_COORDS,

    // buffering
    PF_BUFFER_SIZE,

#ifdef PF_BUG_CATCHING
    PF_BUG,
#endif
};

// Returns descriptive error message.
const char *
pf_error_msg(enum pf_result res);

#define PF_MIN_BUFFER_SIZE 32

struct pf_conn {
    // connection
    int sockfd;

    // accounting
    size_t num_pixels_written;
    size_t num_pixels_read;
};

// TODO pf_connect with already-parsed port and/or address

// Attempts to open a socket and connect to the specified address and port.
// - `addr`: string containing IPv4 address in numbers-and-dots format, e.g. `"127.0.0.1"``
// - `port`: string containing port number
// - `conn`: pointer to the connection that should be initialized (output argument)
enum pf_result
pf_connect_raw(char *addr, char *port, struct pf_conn *conn);

// Closes underlying file descriptor.
void
pf_disconnect(struct pf_conn *conn);

struct pixel {
    uint16_t x;
    uint16_t y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

// --- basic interface ---

// Writes a pixel value, ignoring the alpha value.
// Connection is closed on error.
enum pf_result
pf_put_rgb(struct pf_conn *conn, struct pixel px);

// Writes a pixel value, including alpha.
// Connection is closed on error.
enum pf_result
pf_put_rgba(struct pf_conn *conn, struct pixel px);

// Reads the current canvas size.
// `width` or `height` can be NULL to ignore the value.
// Connection is closed on error.
enum pf_result
pf_get_size(struct pf_conn *conn, uint16_t *width, uint16_t *height);

// Reads a pixel at `px->x` and `px->y`, and inserts the color into `*px`.
// Alpha is set to `0xff`.
// Also checks that the server sent the correct pixel back (matching coordinates).
// Connection is closed on error.
enum pf_result
pf_get(struct pf_conn *conn, struct pixel *px);

// --- buffered interface ---

// Writes many pixel values in a buffered fashion, ignoring the alpha value
// - `pxs`: array of pixels
// - `n`: number of pixels
// - `buf`: buffer to collect commands
// - `buf_size`: size of buffer, in bytes.
//
// The buffer is flushed whenever we can't fit the next command
// Note that the performance of this function is highly dependent on the buffer size.
// At the end of this function, the buffer is guaranteed to be flushed.
//
// Connection is closed on error.
enum pf_result
pf_put_rgb_many(struct pf_conn *conn, struct pixel *pxs, size_t n,
    char *buf, size_t buf_size);

// Same as `pf_put_rgb_many`, but with alpha.
enum pf_result
pf_put_rgba_many(struct pf_conn *conn, struct pixel *pxs, size_t n,
    char *buf, size_t buf_size);

// Reads many pixel values in a buffered fashion.
// - `pxs`: array of pixels
// - `n`: number of pixels
// - `buf`: buffer to collect commands
// - `buf_size`: size of buffer, in bytes.
// - `batch_limit`: Upper limit of how many commands to buffer. `0` means no limit.
//
// For the requests, the `x` and `y` coordinates of the pixels are used.
//
// Both writing the requests and reading the responses are buffered.
//
// This function sends up to `batch_limit` requests before reading the associated
// responses. This means there's a build-up of the response data.
// Depending on the server implementation, it will drop/refuse to handle new requests if
// the server's send buffer is too full. For this reason, the batch limit exists.
// This function only sends up to `batch_limit` requests before reading all the associated
// responses. In this way you can limit the amount of in-flight data.
// Note that this is _not_ possible by shrinking the buffer size -- it still transfers
// _all_ of the requests (now spread over more syscalls, so probably less efficient) before
// reading any response.
//
// Connection is closed on error.
enum pf_result
pf_get_many(struct pf_conn *conn, struct pixel *pxs, size_t n,
    char *buf, size_t buf_size,
    size_t batch_limit);

#endif
