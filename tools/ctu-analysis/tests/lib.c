#include "lib.h"
void SET_COMMON_FORMATS(AVFilterContext *ctx){
     int i=3;
        if (ctx->inputs[i] && !ctx->inputs[i]->out_fmts) {          
             (void)0;                                               
    }                                                       
}

void f(AVFilterContext *ctx){
  SET_COMMON_FORMATS(ctx);
}

/*int div_struct(struct data_t{int a;int b;} *d){
  return d->a/d->b;
}*/

int div_noproto(int d){
    return 100/d;
}

int normal_div(int d){
    return 200/d;
}
