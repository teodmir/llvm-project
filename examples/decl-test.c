#include <stdio.h>
#include <stdlib.h>

void foo(int xs[], int ys[]) {}

float bar(char c1, char c2, int* y) { return 0.5; }

void wrong() {}

struct baz {
    int x;
};

typedef struct {
    char c;
    float x;
    float y;
} qux;

struct wrong {
    int x;
};

int global;

int main(void)
{
    static int static_int;
    int *p = malloc(1000 * sizeof(int));
    p[1];
    free(p);

    goto L;

L:
    return 0;
}
