const int g = 5;
int h = 3;

int addg(int x) {
    return x + g;
}

int main() {
    h = h + 1;
    return addg(h);
}
