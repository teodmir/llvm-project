struct p {
    float x;
    float y;
};

struct p2 {
    struct p a;
    struct p b;
};

void foo(struct p x, int y) {}

int main(void)
{
    return 0;
}
