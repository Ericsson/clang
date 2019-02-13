enum B {x2 = 42,y2,z2};

typedef struct {
  int a;
  int b;
} foobar;

int enumcheck(void) {
  return x2;
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

//Implicit function
int ident_implicit(int in){
    return in;
}

//ASTImporter doesn't support this
int struct_in_proto(struct data_t{int a;int b;} *d){
  return 0;
}
