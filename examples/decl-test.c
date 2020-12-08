#include <stdio.h>
#include <stdlib.h>

void foo(int xs[], int ys[]) {}

float bar(char c1, char c2, int* y) { return 0.5; }

void wrong() {}

int unnamed_test(char c, int x) { return 0; }

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

struct unnamed_test {
    float x;
    float y;
}

int main(void)
{
    return 0;
}
