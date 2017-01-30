
typedef struct {
  int a;
  int b;
} foobar;

foobar fb;

int f(int i) {
  if (fb.a) {
    fb.b = i;
  }
  return 1;
}
