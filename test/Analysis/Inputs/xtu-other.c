enum B {x = 42,l,s};

typedef struct {
  int a;
  int b;
} foobar;

int enumcheck(void) {
  return x;
}

foobar fb;

int f(int i) {
  if (fb.a) {
    fb.b = i;
  }
  return 1;
}

//TEST reporting an
//error in macro
//definition
#define MYMACRO(ctx) \
    ctx->a;
struct S{
  int a;
};

int g(struct S *ctx){  
  MYMACRO(ctx);
  return 0; 
}

// TEST asm import not failing
int getkey() {
  int res;
  asm ( "mov $42, %0"
      : "=r" (res));
  return res;
}
