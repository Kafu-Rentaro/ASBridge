__attribute__((noinline)) int calc(int a,int b){int x=a*b;x=x^7;x=x&0x7fffffff;return x+3;}
int entry_int(void){return calc(6,7);}
