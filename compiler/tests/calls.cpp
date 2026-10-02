volatile unsigned input=9;
__attribute__((noinline)) unsigned third(unsigned x){return x*3+1;}
__attribute__((noinline)) unsigned second(unsigned x){unsigned saved=x+7;return third(x)+saved;}
__attribute__((noinline)) unsigned first(unsigned x){unsigned saved=x*5;return second(x)+saved+third(2);}
extern "C" unsigned kernel(){return first(input);}
