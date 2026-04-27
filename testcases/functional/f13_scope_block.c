int main() {
    int x = 35;
    int y = 1;
    {
        int y = 312;
        x = x + y;
    }
    return y;
}
