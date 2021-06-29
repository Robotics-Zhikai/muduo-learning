#ifndef MUDUO_EXAMPLES_SIMPLE_ECHO_ECHO_H
#define MUDUO_EXAMPLES_SIMPLE_ECHO_ECHO_H

#include "muduo/net/TcpServer.h"

// RFC 862
class EchoServer
{
 public:
  EchoServer(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& listenAddr);

  void start();  // calls server_.start();

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);

  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp time);

  muduo::net::TcpServer server_; 
  //采用基于对象的编程方法，而不是面向对象的编程方法 如果是面向对象的编程方法的话需要继承带有纯虚函数的抽象类然后重新实现
  //但是这个的话是把TCPserver弄成了成员
};

#endif  // MUDUO_EXAMPLES_SIMPLE_ECHO_ECHO_H
