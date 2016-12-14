int callback_to_main(int x);
int f(int x) {
  return x - 1;
}

int g(int x) {
  return callback_to_main(x) + 1;
}

int h_chain(int);

int h(int x) {
  return 2*h_chain(x);
}
