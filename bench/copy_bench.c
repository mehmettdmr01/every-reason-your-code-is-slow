// Madde 10: gereksiz kopyalama. (a) 64 KB struct'ı degerle vs isaretciyle 1M kez fonksiyona gecirmek;
// (b) metni dongude ekleyerek kurmak: her eklemede yeniden ayir+kopyala (karesel) vs kapasite ikiye katlama.
// DCE korumasi: fonksiyon noinline, sonuc toplanip yazdirilir; her olcum 5 tekrar, en kucuk alinir; ilk tur isinma.
// Cikti CSV: test,variant,n,ns_per_op. --quick: daha az yineleme.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define KB64 (64*1024)
typedef struct { uint8_t b[KB64]; } Big;

static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e9+t.tv_nsec; }

__attribute__((noinline)) static uint64_t by_value(Big big, int i){ return big.b[i & (KB64-1)] + big.b[(i*7) & (KB64-1)]; }
__attribute__((noinline)) static uint64_t by_ref(const Big *big, int i){ return big->b[i & (KB64-1)] + big->b[(i*7) & (KB64-1)]; }

static double best(double (*f)(long), long n){ double b=1e30; for(int r=0;r<5;r++){ double t=f(n); if(t<b)b=t; } return b; }

static Big G;
static uint64_t sink;
static double run_value(long n){ double t0=now(); uint64_t s=0; for(long i=0;i<n;i++) s+=by_value(G,(int)i); sink+=s; return (now()-t0)/n; }
static double run_ref(long n){ double t0=now(); uint64_t s=0; for(long i=0;i<n;i++) s+=by_ref(&G,(int)i); sink+=s; return (now()-t0)/n; }

// metin kurma: n parca x 16 bayt
static double run_quadratic(long n){
  double t0=now(); char *s=NULL; size_t len=0;
  for(long i=0;i<n;i++){ char *t=malloc(len+16+1); if(len) memcpy(t,s,len); memcpy(t+len,"0123456789abcdef",16); len+=16; t[len]=0; free(s); s=t; }
  sink+=s[len-1]; free(s); return (now()-t0)/n;
}
static double run_doubling(long n){
  double t0=now(); size_t cap=64,len=0; char *s=malloc(cap);
  for(long i=0;i<n;i++){ if(len+17>cap){ cap*=2; s=realloc(s,cap);} memcpy(s+len,"0123456789abcdef",16); len+=16; s[len]=0; }
  sink+=s[len-1]; free(s); return (now()-t0)/n;
}

int main(int argc,char**argv){
  int quick = argc>1 && !strcmp(argv[1],"--quick");
  for(int i=0;i<KB64;i++) G.b[i]=(uint8_t)i;
  long ncall = quick? 20000 : 1000000;
  long nstr  = quick? 20000 : 200000;
  run_value(ncall/10); run_ref(ncall/10); run_quadratic(nstr/10); run_doubling(nstr/10); // isinma
  printf("test,variant,n,ns_per_op\n");
  printf("call,by_value_64KB,%ld,%.2f\n",ncall,best(run_value,ncall));
  printf("call,by_ref,%ld,%.2f\n",ncall,best(run_ref,ncall));
  printf("string,quadratic_copy,%ld,%.2f\n",nstr,best(run_quadratic,nstr));
  printf("string,doubling,%ld,%.2f\n",nstr,best(run_doubling,nstr));
  fprintf(stderr,"sink=%llu\n",(unsigned long long)sink);
  return 0;
}
