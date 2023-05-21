# Sylar

## 2023-05-15

感觉从IOManager到timer都有问题，不知道协程调度出了问题没，迷茫了。

## 2023-05-16

从协程开始理清思路！！

### 协程

首先是协程，协程本质上可以看看作是一个函数，只是协程在执行过程中可以退出(yield)，并且在适当实际恢复运行(resume)，在这段时间里，其他协程可以获得CPU并运行。协程模块使用的是非对称协程模型，即子协程智能与线程主协程进行切换，而不能与另外一个子协程切换，子协程程序结束后，一定要再切回到主协程。协程模块依赖于ucontext_t实现。

```cpp
typedef struct ucontext_t {
    unsigned long int                    __ctx( uc_flags );
    struct ucontext_t                   *uc_link; //指向上下文，即其他协程
    stack_t                              uc_stack;//该上下文使用的栈
    mcontext_t                           uc_mcontext;//上下文
    sigset_t                             uc_sigmask;//上下文中的阻塞信息
    struct _libc_fpstate                 __fpregs_mem;
    __extension__ unsigned long long int __ssp[ 4 ];
} ucontext_t;
 //获取当前的上下文
int getcontext(ucontext_t *ucp)

//恢复ucp指向的上下文，这个函数不会返回，而是跳转到ucp上，相当于变相的调用了函数
int setcontext(const ucontext_t *ucp) 

//修改由getcontext获取到的上下文指针ucp
//在调用之前，必须手动给ucp分配一段内存空间，存储在ucp->uc_stack中，这段内存空间将作为func函数运行的栈空间
//同时也可以指定ucp->uc_link，表示函数运行结束后恢复uc_link指向的上下文
//如果不复制uc_link,那么func函数结束时必须调用setcontext或swapcontext以重新指定一个有效的上下文，否则程序就跑飞了
//makecontext执行完后，ucp就与函数func绑定了，调用setcontext或swapcontext激活ucp时，func就会被运行
void makecontext(ucontext_t *ucp,void(*func)(),int argc,...)

//恢复ucp指向的上下文，同时将当前的上下文存储到oucp中。
//该函数也不会返回，而是会跳转到ucp上下文对应的函数中执行，相当于调用了函数
int swapcontext(ucontext_t* oucp,const ucontext_t *ucp);
```

#### 协程的实现

对应于线程，协程也设置了五个状态：
