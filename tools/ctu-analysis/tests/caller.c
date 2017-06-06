#include "lib.h"
#include "lib2.h"
void test(){
    AVFilterContext ctx;
    ctx.inputs=0;
    int i=inlineFunction(3,4);
    f(&ctx);
}

void test2(){
    div_ext(0);//error: division by zero
}

void test3(){
    int d=0;
    div_noproto(d);//error division by zero
}

void test4(){
   struct data_t{int a;int b;} d;
   d.a=1;
   d.b=0;
   div_struct(&d);//error: division by zero
}

void test5(){
    normal_div(0);//error:division by zero
}




