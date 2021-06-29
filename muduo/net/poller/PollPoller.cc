// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/poller/PollPoller.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Channel.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller() = default;

Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs); 
  //迭代器里边的元素取首地址，也就是struct取首地址
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0) //说明有一些事件就绪了，为就绪的事件数目
  {
    LOG_TRACE << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels); 
    //一旦有描述符就绪，就把就绪的描述符和就绪类型放到activeChannels中，然后在eventloop中进行处理
  }
  else if (numEvents == 0)
  {
    LOG_TRACE << " nothing happened";
  }
  else
  {
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()";
    }
  }
  return now;//返回就绪事件到来的时间戳
} 

void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
{
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd) //numEvents > 0是为了能够提前退出 提升效率
  {
    if (pfd->revents > 0) //如果这个大于0，说明就产生了事件
    {
      --numEvents;
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end()); //断言通道是存在的，我们在updatechannel中加入数据到map过
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd); //应该是一个保障措施
      channel->set_revents(pfd->revents); //因为发生的事件得到的信息是某一fd发生了某一事件，需要更新对应的channel的revent
      // pfd->revents = 0;
      activeChannels->push_back(channel); //在eventloop::loop中，每次调用poll时都要先把activechannels clear掉
    }
  }
}

void PollPoller::updateChannel(Channel* channel) //注册或者更新通道（原来关注可读可写，现在更新成可读事件）
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0) //如果还不知道本通道在数组中的位置，说明是一个新的通道
  {
    // a new one, add to pollfds_
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    pollfds_.push_back(pfd);
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx); //已经存在的通道其index就不会小于0了
    channels_[pfd.fd] = channel; //更新map
  }
  else
  {  //更新一个已存在的通道
    // update existing one
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events()); //可能主要是要修改这个关注的事件类型
    pfd.revents = 0;
    if (channel->isNoneEvent()) 
    //如果不关注任何事件，那么就表示poll函数需要忽略对应的pollfd结构的成员，需要把fd设置为负值
    {
      // ignore this pollfd
      pfd.fd = -channel->fd()-1; //本来这个直接设置为-1即可，但是设置成这样可以优化removechannel函数
    }
  }
}

void PollPoller::removeChannel(Channel* channel) //调这个之前一定要确保调用过removeall，否则断言会出错
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd();
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  assert(channel->isNoneEvent()); //一定是没有事件才能够移除，否则如果还关注着事件的话就不能移除
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());
  size_t n = channels_.erase(channel->fd());
  assert(n == 1); (void)n;
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
  {
    pollfds_.pop_back();
  }
  else
  {
    //以o(1)的复杂度对vector中的某一元素进行删除，注意需要更新index
    int channelAtEnd = pollfds_.back().fd; //最后一个文件描述符
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
    if (channelAtEnd < 0) 
    //update channel的时候有设置pfd.fd = -channel->fd()-1（暂时把该fd忽略），这里做一个还原
    {
      channelAtEnd = -channelAtEnd-1;
    }
    channels_[channelAtEnd]->set_index(idx); //注意这里要更新的
    pollfds_.pop_back();
  }
}

