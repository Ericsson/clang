typedef struct AVFilterLink    AVFilterLink;
typedef struct AVFilterContext    AVFilterContext;
struct AVFilterLink {
    int  *in_channel_layouts;
    unsigned out_fmts;
};

struct AVFilterContext {
    AVFilterLink **inputs;          ///< array of pointers to input links
    AVFilterLink **outputs;          ///< array of pointers to input links
    unsigned    nb_inputs;          ///< number of input pads
    int nb_threads;
};
void f(AVFilterContext *ctx);
int  inlineFunction(int a, int b){
    return a+b;
}
int normal_div(int d);

//int div_struct(struct data_t{int a; int b;}*);