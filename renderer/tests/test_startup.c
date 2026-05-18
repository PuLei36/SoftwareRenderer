#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/threadpool.h"
#include "test_startup.h"

#ifdef _WIN32
#include <windows.h>
#endif

void startup_configure_console(void) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

static int parse_positive_int(const char *text, int *out_value) {
    char *end;
    long value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 1) {
        return 0;
    }
    *out_value = (int)value;
    return 1;
}

static void trim_newline(char *text) {
    size_t length;

    if (text == NULL) {
        return;
    }
    length = strlen(text);
    while (length > 0 &&
           (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[length - 1] = '\0';
        length -= 1;
    }
}

int startup_prompt_thread_count(void) {
    int default_threads = threadpool_default_threads();
    char line[64];
    int chosen = default_threads;

    for (;;) {
        int parsed;

        printf("渲染线程数 (1=单线程, 默认 %d, 直接回车使用默认): ",
               default_threads);
        fflush(stdout);
        if (fgets(line, (int)sizeof(line), stdin) == NULL) {
            break;
        }
        trim_newline(line);
        if (line[0] == '\0') {
            chosen = default_threads;
            break;
        }
        if (parse_positive_int(line, &parsed)) {
            chosen = parsed;
            break;
        }
        printf("请输入大于 0 的整数。\n");
    }

    return chosen;
}
