/*
 * backend/server.c
 *
 * Small HTTP server for the C Compiler Optimizer.
 *
 * Endpoints:
 *   GET  /          -> frontend/index.html
 *   GET  /index.html
 *   GET  /style.css
 *   GET  /script.js
 *
 *   POST /optimize  -> optimize TAC and return JSON
 *
 * Compile from project root:
 *
 * gcc -Wall -Wextra -std=c11 -O2 \
 * backend/server.c \
 * tac.c analyzer.c optimizer.c verifier.c cost.c \
 * orchestrator.c dataset.c logger.c csv.c \
 * -o backend/server
 *
 * Run:
 * ./backend/server
 *
 * Open:
 * http://localhost:8081
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/wait.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "../tac.h"
#include "../orchestrator.h"
#include "../cost.h"
#include "../verifier.h"
#include "../c_frontend.h"
#include "../csv.h"

#define BUFFER_SIZE (1024 * 1024 + 8192)
static int next_web_program_id = 100000;

/* ---------------------------------------------------------
   Utility: send complete string
   --------------------------------------------------------- */

static void send_all(int client, const char *data)
{
    size_t total = 0;
    size_t length = strlen(data);

    while (total < length)
    {
        ssize_t sent = send(
            client,
            data + total,
            length - total,
            0
        );

        if (sent <= 0)
            return;

        total += (size_t)sent;
    }
}

/* ---------------------------------------------------------
   HTTP response
   --------------------------------------------------------- */

static void send_response(
    int client,
    const char *content_type,
    const char *body
)
{
    char header[1024];

    int body_length = (int)strlen(body);

    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        content_type,
        body_length
    );

    send_all(client, header);
    send_all(client, body);
}

/* ---------------------------------------------------------
   404 response
   --------------------------------------------------------- */

static void send_404(int client)
{
    const char *body =
        "<h1>404 Not Found</h1>";

    char header[512];

    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        strlen(body)
    );

    send_all(client, header);
    send_all(client, body);
}

/* ---------------------------------------------------------
   JSON escaping
   --------------------------------------------------------- */

static void json_escape(
    const char *input,
    char *output,
    size_t output_size
)
{
    size_t j = 0;

    for (size_t i = 0;
         input[i] != '\0' && j + 2 < output_size;
         i++)
    {
        char c = input[i];

        if (c == '"')
        {
            if (j + 2 >= output_size)
                break;

            output[j++] = '\\';
            output[j++] = '"';
        }
        else if (c == '\\')
        {
            if (j + 2 >= output_size)
                break;

            output[j++] = '\\';
            output[j++] = '\\';
        }
        else if (c == '\n')
        {
            if (j + 2 >= output_size)
                break;

            output[j++] = '\\';
            output[j++] = 'n';
        }
        else if (c == '\r')
        {
            if (j + 2 >= output_size)
                break;

            output[j++] = '\\';
            output[j++] = 'r';
        }
        else if (c == '\t')
        {
            if (j + 2 >= output_size)
                break;

            output[j++] = '\\';
            output[j++] = 't';
        }
        else if ((unsigned char)c < 32)
        {
            /* Ignore other control characters */
        }
        else
        {
            output[j++] = c;
        }
    }

    output[j] = '\0';
}

/* ---------------------------------------------------------
   Parse TAC text
 *
 * Supported formats:
 *
 *   t1 = 2 + 3
 *   t2 = t1 * 4
 *   x = t2
 *
 * Also accepts:
 *
 *   t1 = 2
 *
 * --------------------------------------------------------- */

static int parse_tac(
    const char *text,
    TACProgram *program,
    char *error,
    size_t error_size
)
{
    init_program(program, 1, "Frontend");

    char *copy = malloc(strlen(text) + 1);

    if (copy == NULL)
        return 0;

    strcpy(copy, text);

    char *line = strtok(copy, "\n");
    int line_number = 0;

    while (line != NULL)
    {
        line_number++;
        char result[MAX_OPERAND] = "";
        char arg1[MAX_OPERAND] = "";
        char op[8] = "";
        char arg2[MAX_OPERAND] = "";

        /* Remove leading spaces */
        while (isspace((unsigned char)*line))
            line++;

        /* Remove trailing spaces */
        size_t len = strlen(line);

        while (len > 0 &&
               isspace((unsigned char)line[len - 1]))
        {
            line[len - 1] = '\0';
            len--;
        }

        /* Ignore empty lines */
        if (strlen(line) == 0)
        {
            line = strtok(NULL, "\n");
            continue;
        }

        /*
         * Ignore comments.
         */
        if (line[0] == '#')
        {
            line = strtok(NULL, "\n");
            continue;
        }

        if (strncmp(line, "return ", 7) == 0)
        {
            char value[MAX_OPERAND], extra[2];
            if (sscanf(line + 7, "%31s %1s", value, extra) != 1 || program->has_return)
            {
                snprintf(error, error_size, "invalid or duplicate return at line %d", line_number);
                free(copy);
                return 0;
            }
            snprintf(program->output, sizeof(program->output), "%s", value);
            program->has_return = 1;
            line = strtok(NULL, "\n");
            while (line != NULL)
            {
                while (isspace((unsigned char)*line)) line++;
                if (*line && *line != '#')
                {
                    snprintf(error, error_size, "return must be the last instruction (line %d)", line_number + 1);
                    free(copy);
                    return 0;
                }
                line = strtok(NULL, "\n");
                line_number++;
            }
            break;
        }

        /*
         * Try arithmetic instruction:
         *
         * t1 = a + b
         */
        int consumed = 0;
        int matched = sscanf(
            line,
            " %31s = %31s %7s %31s %n",
            result,
            arg1,
            op,
            arg2,
            &consumed
        );

        if (matched == 4 && line[consumed] == '\0' &&
            (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") || !strcmp(op, "/")))
        {
            if (program->count >= MAX_INSTRUCTIONS)
            {
                free(copy);
                return 0;
            }

            if (!add_instruction(
                    program,
                    result,
                    arg1,
                    op,
                    arg2))
            {
                free(copy);
                return 0;
            }
        }
        else
        {
            /*
             * Try simple assignment:
             *
             * x = y
             */
            consumed = 0;
            matched = sscanf(
                line,
                " %31s = %31s %n",
                result,
                arg1,
                &consumed
            );

            if (matched == 2 && line[consumed] == '\0')
            {
                if (program->count >= MAX_INSTRUCTIONS)
                {
                    free(copy);
                    return 0;
                }

                if (!add_instruction(
                        program,
                        result,
                        arg1,
                        "=",
                        ""))
                {
                    free(copy);
                    return 0;
                }
            }
            else
            {
                snprintf(error, error_size, "invalid TAC at line %d", line_number);
                free(copy);
                return 0;
            }
        }

        line = strtok(NULL, "\n");
    }

    free(copy);

    /*
     * The last assigned variable is used as output.
     */
    if (!program->output[0] && program->count > 0)
    {
        strcpy(
            program->output,
            program->code[program->count - 1].result
        );
    }

    if (program->count == 0)
    {
        snprintf(error, error_size, "TAC program contains no instructions");
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------
   Convert TAC program back to text
   --------------------------------------------------------- */

static void tac_to_text(
    const TACProgram *program,
    char *output,
    size_t output_size
)
{
    output[0] = '\0';

    size_t used = 0;

    for (int i = 0;
         i < program->count;
         i++)
    {
        const TACInstruction *ins =
            &program->code[i];

        int written;

        if (strcmp(ins->op, "=") == 0)
        {
            written = snprintf(
                output + used,
                output_size - used,
                "%s = %s\n",
                ins->result,
                ins->arg1
            );
        }
        else
        {
            written = snprintf(
                output + used,
                output_size - used,
                "%s = %s %s %s\n",
                ins->result,
                ins->arg1,
                ins->op,
                ins->arg2
            );
        }

        if (written < 0)
            break;

        if ((size_t)written >= output_size - used)
        {
            used = output_size - 1;
            break;
        }

        used += (size_t)written;
    }
    if (program->has_return && used < output_size - 1)
        snprintf(output + used, output_size - used, "return %s\n", program->output);
}

/* ---------------------------------------------------------
   Compare original and proposed TAC
   --------------------------------------------------------- */

static void handle_compare(int client, const char *body)
{
    const char *separator = "\n--CANDIDATE--\n";
    const char *cut = strstr(body, separator);
    if (!cut) { send_response(client, "application/json", "{\"success\":false,\"error\":\"Provide original TAC, then --CANDIDATE--, then candidate TAC.\"}"); return; }
    size_t original_len = (size_t)(cut - body);
    char *original_text = malloc(original_len + 1);
    if (!original_text) { send_response(client, "application/json", "{\"success\":false,\"error\":\"Server memory allocation failed\"}"); return; }
    memcpy(original_text, body, original_len); original_text[original_len] = '\0';
    TACProgram original, candidate;
    char error[256] = "";
    if (!parse_tac(original_text, &original, error, sizeof(error))) {
        char escaped[512], json[768]; json_escape(error, escaped, sizeof(escaped));
        snprintf(json, sizeof(json), "{\"success\":false,\"error\":\"Original TAC: %s\"}", escaped);
        send_response(client, "application/json", json); free(original_text); return;
    }
    error[0] = '\0';
    if (!parse_tac(cut + strlen(separator), &candidate, error, sizeof(error))) {
        char escaped[512], json[768]; json_escape(error, escaped, sizeof(escaped));
        snprintf(json, sizeof(json), "{\"success\":false,\"error\":\"Candidate TAC: %s\"}", escaped);
        send_response(client, "application/json", json); free(original_text); return;
    }
    free(original_text);
    original.id = candidate.id = next_web_program_id++;
    VerificationResult verification = verify_programs(&original, &candidate, 20, 0);
    CostMetrics old_cost = calculate_cost(&original), new_cost = calculate_cost(&candidate);
    int reduced = new_cost.cost < old_cost.cost;
    int accepted = verification.passed && reduced;
    csv_open();
    csv_program_result(&original, &candidate, old_cost.cost, new_cost.cost, 0, verification.passed);
    csv_close();
    double reduction = old_cost.cost > 0
        ? 100.0 * ((double)old_cost.cost - (double)new_cost.cost) / (double)old_cost.cost
        : 0.0;
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"success\":true,\"verification\":%s,\"output_match\":%s,\"original_cost\":%d,\"candidate_cost\":%d,\"cost_reduction\":%.2f,\"original_instructions\":%d,\"candidate_instructions\":%d,\"original_arithmetic\":%d,\"candidate_arithmetic\":%d,\"original_temporaries\":%d,\"candidate_temporaries\":%d,\"cost_reduced\":%s,\"decision\":\"%s\",\"reason\":\"%s\"}",
        verification.passed ? "true" : "false", verification.passed ? "true" : "false",
        old_cost.cost, new_cost.cost, reduction, old_cost.instructions, new_cost.instructions,
        old_cost.arithmetic, new_cost.arithmetic, old_cost.temporaries, new_cost.temporaries,
        reduced ? "true" : "false", accepted ? "ACCEPT" : "REJECT",
        !verification.passed ? "Candidate failed differential verification" : (reduced ? "Candidate is verified and lowers cost" : "Candidate cost is not lower"));
    send_response(client, "application/json", json);
}

/* ---------------------------------------------------------
   Handle optimization request
   --------------------------------------------------------- */

static void handle_optimize(
    int client,
    const char *body,
    int is_c_source
)
{
    TACProgram original;
    TACProgram optimized;
    char parse_error[256] = "";

    /*
     * Parse input TAC.
     */
    int parsed = is_c_source
        ? lower_c_source(body, &original, parse_error, sizeof(parse_error))
        : parse_tac(body, &original, parse_error, sizeof(parse_error));
    if (!parsed)
    {
        char safe_error[512], error_json[768];
        if (!parse_error[0]) snprintf(parse_error, sizeof(parse_error), "Could not parse TAC program");
        json_escape(parse_error, safe_error, sizeof(safe_error));
        snprintf(error_json, sizeof(error_json), "{\"success\":false,\"error\":\"%s\"}", safe_error);

        send_response(
            client,
            "application/json",
            error_json
        );

        return;
    }
    original.id = next_web_program_id++;

    /*
     * Copy original program.
     *
     * The optimizer modifies this copy.
     */
    copy_program(
        &original,
        &optimized
    );

    /*
     * Calculate original cost.
     */
    CostMetrics original_metrics =
        calculate_cost(&original);

    /*
     * Run the complete optimization loop.
     *
     * ANALYZE
     *    ↓
     * PROPOSE
     *    ↓
     * VERIFY
     *    ↓
     * COST
     *    ↓
     * DECISION
     *    ↓
     * repeat
     */
    OptimizationSummary summary =
        optimize_program(
            &optimized,
            0
        );

    /*
     * Calculate final cost.
     */
    CostMetrics final_metrics =
        calculate_cost(&optimized);

    csv_open();
    csv_program_result(&original, &optimized, original_metrics.cost, final_metrics.cost, summary.iterations, summary.output_match);
    csv_close();

    /*
     * Extract required values.
     */
    int original_cost =
        original_metrics.cost;

    int final_cost =
        final_metrics.cost;

    int output_match =
        summary.output_match;

    /*
     * Calculate percentage reduction.
     */
    double reduction = 0.0;

    if (original_cost > 0)
    {
        reduction =
            ((double)(original_cost - final_cost)
             / (double)original_cost) * 100.0;
    }

    /*
     * Convert programs to text.
     */
    char original_tac[32768];
    char optimized_tac[32768];

    tac_to_text(
        &original,
        original_tac,
        sizeof(original_tac)
    );

    tac_to_text(
        &optimized,
        optimized_tac,
        sizeof(optimized_tac)
    );

    /*
     * Escape TAC for JSON.
     */
    char original_json[65536];
    char optimized_json[65536];

    json_escape(
        original_tac,
        original_json,
        sizeof(original_json)
    );

    json_escape(
        optimized_tac,
        optimized_json,
        sizeof(optimized_json)
    );

    /*
     * Create JSON response.
     */
    char *json =
        malloc(145000);

    if (json == NULL)
    {
        const char *error_json =
            "{"
            "\"success\":false,"
            "\"error\":\"Server memory allocation failed\""
            "}";

        send_response(
            client,
            "application/json",
            error_json
        );

        return;
    }

    snprintf(
        json,
        145000,

        "{"
        "\"success\":true,"

        "\"original_tac\":\"%s\","
        "\"optimized_tac\":\"%s\","

        "\"original_cost\":%d,"
        "\"optimized_cost\":%d,"
        "\"cost_reduction\":%.2f,"

        "\"iterations\":%d,"
        "\"proposals\":%d,"
        "\"verified\":%d,"
        "\"accepted\":%d,"
        "\"rejected\":%d,"

        "\"verification\":%s,"
        "\"output_match\":%s"

        "}",

        original_json,
        optimized_json,

        original_cost,
        final_cost,
        reduction,

        summary.iterations,
        summary.proposals,
        summary.verified,
        summary.accepted,
        summary.rejected,

        output_match ? "true" : "false",
        output_match ? "true" : "false"
    );

    send_response(
        client,
        "application/json",
        json
    );

    free(json);
}

/* ---------------------------------------------------------
   Read requested static file
   --------------------------------------------------------- */

static void serve_file(
    int client,
    const char *path,
    const char *content_type
)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
    {
        send_404(client);
        return;
    }

    fseek(file, 0, SEEK_END);

    long size = ftell(file);

    fseek(file, 0, SEEK_SET);

    if (size < 0)
    {
        fclose(file);
        send_404(client);
        return;
    }

    char *data =
        malloc((size_t)size + 1);

    if (data == NULL)
    {
        fclose(file);
        send_404(client);
        return;
    }

    size_t read_count =
        fread(
            data,
            1,
            (size_t)size,
            file
        );

    fclose(file);

    data[read_count] = '\0';

    send_response(
        client,
        content_type,
        data
    );

    free(data);
}

/* ---------------------------------------------------------
   Extract HTTP body
   --------------------------------------------------------- */

static char *extract_body(
    char *request
)
{
    char *body =
        strstr(request, "\r\n\r\n");

    if (body != NULL)
        return body + 4;

    body =
        strstr(request, "\n\n");

    if (body != NULL)
        return body + 2;

    return NULL;
}

/* ---------------------------------------------------------
   Handle HTTP request
   --------------------------------------------------------- */

static void handle_request(
    int client,
    char *request
)
{
    if (strncmp(request, "POST /compare-tac ", 18) == 0)
    {
        char *body = extract_body(request);
        if (!body) { send_404(client); return; }
        handle_compare(client, body);
        return;
    }
    if (strncmp(request, "GET /health ", 12) == 0)
    {
        send_response(client, "application/json", "{\"success\":true}");
        return;
    }

    if (strncmp(request, "POST /compile-c ", 16) == 0 ||
        strncmp(request, "POST /optimize ", 15) == 0)
    {
        char *body = extract_body(request);

        if (body == NULL)
        {
            send_404(client);
            return;
        }

        int is_c = strncmp(request, "POST /compile-c", 15) == 0;
        handle_optimize(client, body, is_c);

        return;
    }

    /*
     * GET /
     */
    if (strncmp(
            request,
            "GET / ",
            6
        ) == 0)
    {
        serve_file(
            client,
            "frontend/index.html",
            "text/html; charset=utf-8"
        );

        return;
    }

    /*
     * GET /index.html
     */
    if (strncmp(
            request,
            "GET /index.html",
            15
        ) == 0)
    {
        serve_file(
            client,
            "frontend/index.html",
            "text/html; charset=utf-8"
        );

        return;
    }

    /*
     * GET /style.css
     */
    if (strncmp(
            request,
            "GET /style.css",
            14
        ) == 0)
    {
        serve_file(
            client,
            "frontend/style.css",
            "text/css; charset=utf-8"
        );

        return;
    }

    /*
     * GET /script.js
     */
    if (strncmp(
            request,
            "GET /script.js",
            14
        ) == 0)
    {
        serve_file(
            client,
            "frontend/script.js",
            "application/javascript; charset=utf-8"
        );

        return;
    }

    send_404(client);
}

/* ---------------------------------------------------------
   Main HTTP server
   --------------------------------------------------------- */

int main(void)
{
    int port = 8081;
    const char *port_env = getenv("COMPILER_PORT");
    if (port_env && *port_env)
    {
        char *end = NULL;
        long configured = strtol(port_env, &end, 10);
        if (!end || *end || configured < 1 || configured > 65535)
        {
            fprintf(stderr, "Invalid COMPILER_PORT value: %s\n", port_env);
            return 1;
        }
        port = (int)configured;
    }
    int server_fd;

    /*
     * Create TCP socket.
     */
    server_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /*
     * Allow reuse of port.
     */
    int opt = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)
        ) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    /*
     * Configure address.
     */
    struct sockaddr_in address;

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        inet_addr("127.0.0.1");

    address.sin_port =
        htons((unsigned short)port);

    /*
     * Bind.
     */
    if (bind(
            server_fd,
            (struct sockaddr *)&address,
            sizeof(address)
        ) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    /*
     * Listen.
     */
    if (listen(
            server_fd,
            10
        ) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("\n");
    printf("=============================================\n");
    printf("   C COMPILER OPTIMIZER SERVER\n");
    printf("=============================================\n");
    printf("\n");
    printf("Server running at:\n");
    printf("http://localhost:%d\n", port);
    printf("\n");
    printf("Frontend:\n");
    printf("frontend/index.html\n");
    printf("\n");
    printf("POST endpoints:\n");
    printf("/optimize (TAC)\n/compile-c (C source)\n");
    printf("\n");
    printf("Press Ctrl+C to stop.\n");
    printf("\n");

    /*
     * Infinite server loop.
     */
    while (1)
    {
        struct sockaddr_in client_address;

        socklen_t client_length =
            sizeof(client_address);

        int client =
            accept(
                server_fd,
                (struct sockaddr *)&client_address,
                &client_length
            );

        if (client < 0)
        {
            perror("accept");
            continue;
        }

        /*
         * Receive HTTP request.
         *
         * The frontend sends small TAC programs,
         * so a single buffer is sufficient for the
         * college demonstration.
         */
        char *request = calloc(BUFFER_SIZE + 1, 1);
        if (request == NULL)
        {
            close(client);
            continue;
        }

        size_t received = 0, body_start = 0, content_length = 0;
        int have_headers = 0;
        while (received < BUFFER_SIZE)
        {
            ssize_t n = recv(client, request + received, BUFFER_SIZE - received, 0);
            if (n <= 0) break;
            received += (size_t)n;
            request[received] = '\0';
            char *header_end = strstr(request, "\r\n\r\n");
            if (!header_end) header_end = strstr(request, "\n\n");
            if (header_end && !have_headers)
            {
                body_start = (size_t)(header_end - request) + (header_end[0] == '\r' ? 4 : 2);
                have_headers = 1;
                char *line = strstr(request, "\r\n");
                if (!line) line = strstr(request, "\n");
                if (line)
                {
                    line += line[0] == '\r' ? 2 : 1;
                    while (line < header_end)
                    {
                        char *next = strstr(line, "\r\n");
                        size_t step = 2;
                        if (!next) { next = strchr(line, '\n'); step = 1; }
                        if (!next || next > header_end) next = header_end;
                        if (strncasecmp(line, "Content-Length:", 15) == 0)
                            content_length = (size_t)strtoul(line + 15, NULL, 10);
                        line = next + step;
                    }
                }
            }
            if (have_headers && received >= body_start + content_length) break;
        }
        if (received > 0 && have_headers && received >= body_start + content_length)
            handle_request(client, request);
        else
            send_response(client, "application/json", "{\"success\":false,\"error\":\"Incomplete or oversized request\"}");

        free(request);
        close(client);
    }

    close(server_fd);

    return 0;
}
