__attribute__((noinline)) unsigned long add2(unsigned long a,unsigned long b){return a+b;}
unsigned long entry(void){return add2(100,200);}
