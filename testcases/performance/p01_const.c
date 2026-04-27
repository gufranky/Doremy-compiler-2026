int compute(int x, int y) {
    int a = 22059;
    int b = 6698;
    int c = -6078;
    int p = a + 24898;
    int q = b * 6338;
    int r = c - 23526;
    int result = p * q / (r - 1);
    return result;
}

int main() {
    int N = 20000000;
    int MOD = 998244353;
    int i = 0;
    int result = 0;
    while (i < N) {
        result = ((result + compute((i + 909990148) % 32768, (i + 369319644) % 32768) % MOD) % MOD - i % MOD) % MOD;
        i = i + 1;
    }
    return result;
}
