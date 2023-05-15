
/**
 * @brief 创建额外的文件描述符，来唯一标识内核中的这个事件
 *
 * @param size 告诉内核事件表需要多大。
 * @return int 用于其他所有epoll系统调用的第一个参数，以指定要访问的内核事件表
 */
int epoll_create( int size );

/**
 * @brief 操作epoll的内核事件表
 *
 * @param epfd:事件表
 * @param op: 指定操作类型: EPOLL_CTL_ADD往事件表中注册fd上的事件
                           EPOLL_CTL_MOD修改fd上的注册事件
                           EPOLL_CTL_DEL删除fd上的注册事件
 * @param fd: 要操作的文件描述符
 * @param event:指定事件
 * @return int:操作成功返回0，失败返回-1,并设置errno
 */
int epoll_ctl( int epfd, int op, int fd, struct epoll_event *event );

/**
 * @brief  :在一段超时时间内等待一组文件描述符上的事件,
 如果检测到事件，就将所有就绪的事件从内核事件表中复制到它的第二个参数events指向的数组中。
 * @param epfd:内核事件表
 * @param events:指定事件
 * @param maxevents:最多监听的事件个数
 * @param timeout:超时时间设置
 * @return int:函数成功时返回就绪的文件描述符的个数，失败时返回-1并设置errno
 */
int epoll_wait( int epfd, struct epoll_event *events, int maxevents,
                int timeout );