int main() {
    int x = 0;
    while (x < 8) {
        if (x % 2 == 0) {
            x = x + 3;
        } else {
            x = x - 1;
        }
    }
    return x;
}
