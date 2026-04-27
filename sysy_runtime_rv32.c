#include <stdint.h>
#include <stddef.h>

#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_EXIT 93

static char in_buf[4096];
static int in_pos = 0;
static int in_len = 0;
static int pushback_ch = -1;

static char out_buf[4096];
static int out_len = 0;

static long sys_call3(long n, long a0, long a1, long a2) {
  register long x10 asm("a0") = a0;
  register long x11 asm("a1") = a1;
  register long x12 asm("a2") = a2;
  register long x17 asm("a7") = n;
  asm volatile("ecall" : "+r"(x10) : "r"(x11), "r"(x12), "r"(x17) : "memory");
  return x10;
}

static long sys_read(int fd, void* buf, unsigned long count) {
  return sys_call3(SYS_READ, fd, (long)buf, count);
}

static long sys_write(int fd, const void* buf, unsigned long count) {
  return sys_call3(SYS_WRITE, fd, (long)buf, count);
}

static void emit_char(char ch);

void __sysy_flush_output(void) {
  if (out_len > 0) {
    sys_write(1, out_buf, (unsigned long)out_len);
    out_len = 0;
  }
}

static void emit_char(char ch) {
  if (out_len >= (int)sizeof(out_buf)) {
    __sysy_flush_output();
  }
  out_buf[out_len++] = ch;
}

static void emit_str(const char* s) {
  while (*s) {
    emit_char(*s++);
  }
}

static void emit_uint(unsigned int value) {
  char buf[16];
  int n = 0;
  do {
    buf[n++] = (char)('0' + (value % 10));
    value /= 10;
  } while (value != 0);
  while (n-- > 0) {
    emit_char(buf[n]);
  }
}

static int next_char(void) {
  if (pushback_ch >= 0) {
    int ch = pushback_ch;
    pushback_ch = -1;
    return ch;
  }
  if (in_pos >= in_len) {
    long got = sys_read(0, in_buf, sizeof(in_buf));
    if (got <= 0) {
      return -1;
    }
    in_pos = 0;
    in_len = (int)got;
  }
  return (unsigned char)in_buf[in_pos++];
}

static void unread_char(int ch) {
  pushback_ch = ch;
}

static int skip_spaces(void) {
  int ch = next_char();
  while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\v' || ch == '\f') {
    ch = next_char();
  }
  return ch;
}

int getch(void) {
  return next_char();
}

int getint(void) {
  int ch = skip_spaces();
  int sign = 1;
  int value = 0;
  if (ch == '-') {
    sign = -1;
    ch = next_char();
  } else if (ch == '+') {
    ch = next_char();
  }
  while (ch >= '0' && ch <= '9') {
    value = value * 10 + (ch - '0');
    ch = next_char();
  }
  if (ch >= 0) {
    unread_char(ch);
  }
  return sign * value;
}

static double pow10_int(int exp) {
  double base = 10.0;
  double result = 1.0;
  int e = exp < 0 ? -exp : exp;
  while (e > 0) {
    if (e & 1) {
      result *= base;
    }
    base *= base;
    e >>= 1;
  }
  return exp < 0 ? 1.0 / result : result;
}

float getfloat(void) {
  int ch = skip_spaces();
  int sign = 1;
  if (ch == '-') {
    sign = -1;
    ch = next_char();
  } else if (ch == '+') {
    ch = next_char();
  }

  double value = 0.0;
  while (ch >= '0' && ch <= '9') {
    value = value * 10.0 + (double)(ch - '0');
    ch = next_char();
  }

  if (ch == '.') {
    double frac = 0.0;
    double scale = 1.0;
    ch = next_char();
    while (ch >= '0' && ch <= '9') {
      frac = frac * 10.0 + (double)(ch - '0');
      scale *= 10.0;
      ch = next_char();
    }
    value += frac / scale;
  }

  if (ch == 'e' || ch == 'E') {
    int exp_sign = 1;
    int exp_value = 0;
    ch = next_char();
    if (ch == '-') {
      exp_sign = -1;
      ch = next_char();
    } else if (ch == '+') {
      ch = next_char();
    }
    while (ch >= '0' && ch <= '9') {
      exp_value = exp_value * 10 + (ch - '0');
      ch = next_char();
    }
    value *= pow10_int(exp_sign * exp_value);
  }

  if (ch >= 0) {
    unread_char(ch);
  }
  if (sign < 0) {
    value = -value;
  }
  return (float)value;
}

int getarray(int a[]) {
  int n = getint();
  for (int i = 0; i < n; ++i) {
    a[i] = getint();
  }
  return n;
}

int getfarray(float a[]) {
  int n = getint();
  for (int i = 0; i < n; ++i) {
    a[i] = getfloat();
  }
  return n;
}

void putint(int x) {
  unsigned int value;
  if (x < 0) {
    emit_char('-');
    value = (unsigned int)(-(x + 1)) + 1u;
  } else {
    value = (unsigned int)x;
  }
  emit_uint(value);
}

void putch(int x) {
  emit_char((char)(x & 0xff));
}

static void emit_hex_upto_6(uint32_t value) {
  static const char* digits = "0123456789abcdef";
  char buf[6];
  for (int i = 5; i >= 0; --i) {
    buf[i] = digits[value & 0xfu];
    value >>= 4;
  }
  int end = 6;
  while (end > 0 && buf[end - 1] == '0') {
    --end;
  }
  if (end == 0) {
    return;
  }
  emit_char('.');
  for (int i = 0; i < end; ++i) {
    emit_char(buf[i]);
  }
}

void putfloat(float x) {
  union {
    float f;
    uint32_t u;
  } v;
  v.f = x;

  uint32_t sign = v.u >> 31;
  uint32_t exp = (v.u >> 23) & 0xffu;
  uint32_t frac = v.u & 0x7fffffu;

  if ((v.u & 0x7fffffffu) == 0) {
    if (sign) {
      emit_char('-');
    }
    emit_str("0x0p+0");
    return;
  }

  if (sign) {
    emit_char('-');
  }

  if (exp == 0xffu) {
    if (frac == 0) {
      emit_str("inf");
    } else {
      emit_str("nan");
    }
    return;
  }

  int actual_exp;
  uint32_t frac24;
  if (exp == 0) {
    actual_exp = -126;
    while ((frac & 0x800000u) == 0) {
      frac <<= 1;
      --actual_exp;
    }
    frac &= 0x7fffffu;
    frac24 = frac << 1;
  } else {
    actual_exp = (int)exp - 127;
    frac24 = frac << 1;
  }

  emit_str("0x1");
  emit_hex_upto_6(frac24);
  emit_char('p');
  if (actual_exp >= 0) {
    emit_char('+');
    emit_uint((unsigned int)actual_exp);
  } else {
    emit_char('-');
    emit_uint((unsigned int)(-actual_exp));
  }
}

void putarray(int n, int a[]) {
  putint(n);
  emit_char(':');
  if (n > 0) {
    emit_char(' ');
  }
  for (int i = 0; i < n; ++i) {
    if (i > 0) {
      emit_char(' ');
    }
    putint(a[i]);
  }
}

void putfarray(int n, float a[]) {
  putint(n);
  emit_char(':');
  if (n > 0) {
    emit_char(' ');
  }
  for (int i = 0; i < n; ++i) {
    if (i > 0) {
      emit_char(' ');
    }
    putfloat(a[i]);
  }
}

void starttime(void) {}
void stoptime(void) {}
