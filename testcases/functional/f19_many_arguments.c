int sum8(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
    return a1 - a2 + a3 + a4 - a5 + a6 + a7 - a8;
}

int sum16(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
          int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16) {
    return a1 + a2 + a3 - a4 + a5 + a6 + a7 + a8 +
           a9 + a10 - a11 + a12 - a13 - a14 + a15 + a16;
}

int sum32(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
          int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16,
          int a17, int a18, int a19, int a20, int a21, int a22, int a23, int a24,
          int a25, int a26, int a27, int a28, int a29, int a30, int a31, int a32) {
    int sum1 = a1 - a2 - a3 + a4 - a5 - a6 - a7 - a8;
    int sum2 = a9 - a10 - a11 + a12 - a13 + a14 + a15 - a16;
    int sum3 = a17 + a18 + a19 + a20 - a21 - a22 - a23 - a24;
    int sum4 = a25 + a26 - a27 - a28 + a29 - a30 - a31 - a32;
    return sum1 + sum2 - sum3 + sum4;
}

int sum64(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
          int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16,
          int a17, int a18, int a19, int a20, int a21, int a22, int a23, int a24,
          int a25, int a26, int a27, int a28, int a29, int a30, int a31, int a32,
          int a33, int a34, int a35, int a36, int a37, int a38, int a39, int a40,
          int a41, int a42, int a43, int a44, int a45, int a46, int a47, int a48,
          int a49, int a50, int a51, int a52, int a53, int a54, int a55, int a56,
          int a57, int a58, int a59, int a60, int a61, int a62, int a63, int a64) {
    int sum1 = a1 + a2 + a3 + a4 + a5 - a6 + a7 + a8;
    int sum2 = a9 + a10 - a11 + a12 + a13 - a14 + a15 + a16;
    int sum3 = a17 - a18 + a19 - a20 + a21 + a22 + a23 - a24;
    int sum4 = a25 - a26 - a27 - a28 + a29 - a30 - a31 - a32;
    int sum5 = a33 + a34 + a35 + a36 + a37 + a38 - a39 - a40;
    int sum6 = a41 + a42 + a43 - a44 + a45 + a46 - a47 + a48;
    int sum7 = a49 + a50 + a51 + a52 - a53 + a54 - a55 + a56;
    int sum8 = a57 + a58 + a59 - a60 + a61 + a62 + a63 - a64;
    return sum1 + sum2 - sum3 - sum4 + sum5 - sum6 - sum7 + sum8;
}

int main() {
    int v1 = 1;
    int v2 = 2;
    int v3 = 3;
    int v4 = 4;
    int v5 = 5;
    int v6 = 6;
    int v7 = 7;
    int v8 = 8;
    int v9 = 9;
    int v10 = 277;
    int v11 = 945;
    int v12 = 372;
    int v13 = 102;
    int v14 = 532;
    int v15 = 776;
    int v16 = 670;

    int result1 = sum8(v1, 433, v3, 931, v5, 669, v7, 691);

    int result2 = sum16(v1, v2, v3, v4, v5, v6, v7, v8,
                        776, 569, 863, 516, result1 - v13, result1 + v14, result1 + v15, result1 + v16);

    int v17 = 39;
    int v18 = 106;
    int v19 = 716;
    int v20 = 459;
    int v21 = 140;
    int v22 = 82;
    int v23 = 63;
    int v24 = 506;
    int v25 = 408;
    int v26 = 41;
    int v27 = 312;
    int v28 = 488;
    int v29 = 258;
    int v30 = 969;
    int v31 = 234;
    int v32 = 446;

    int result3 = sum32(
        v1, v2, v3, v4, v5, v6, v7, v8,
        v9, v10, v11, v12, v13, v14, v15, v16,
        v17, v18, v19, v20, v21, v22, v23, v24,
        v25, v26, v27, v28, v29, v30, v31, v32);

    int result4 = sum64(
        v1, v2, v3, v4, v5, v6, v7, v8,
        952, 524, 755, 343, 234, 335, 637, 221,
        v17, v18, v19, v20, v21, v22, v23, v24,
        52, 638, 768, 812, 406, 155, 497, 208,
        v1 + 247, v2 - 711, v3 + 758, v4 - 698, v5 - 860, v6 + 216, v7 + 741, v8 + 313,
        v9 * 891, v10 * 360, v11 * 553, v12 * 990, v13 * 952, v14 * 892, v15 * 549, v16 * 660,
        v1 - v17, v2 - v18, v3 + v19, v4 + v20, v5 - v21, v6 + v22, v7 + v23, v8 + v24,
        v1 * v9 - result3, v2 * v10 + result3, v3 * v11 + result3, v4 * v12 - result3, v5 * v13 + result3, v6 * v14 - result3, v7 * v15 + result3, v8 * v16 +(result1 + result2 + result3));

    int final_result = result1 + result2 - result3 - result4;

    return final_result % 1199;
}
