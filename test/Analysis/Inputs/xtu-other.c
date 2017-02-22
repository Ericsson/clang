
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
