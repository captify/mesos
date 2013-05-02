/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <signal.h>

#include <gtest/gtest.h>

#include <queue>

#include <tr1/functional>

#include <jvm/jvm.hpp>

#include <jvm/org/apache/log4j.hpp>
#include <jvm/org/apache/log4j.hpp>

#include <stout/lambda.hpp>

#include "common/lock.hpp"

#include "logging/logging.hpp"

#include "tests/utils.hpp"
#include "tests/zookeeper_test.hpp"
#include "tests/zookeeper_test_server.hpp"

namespace mesos {
namespace internal {
namespace tests {

const Duration ZooKeeperTest::NO_TIMEOUT = Milliseconds(5000);


void ZooKeeperTest::SetUpTestCase()
{
  if (!Jvm::created()) {
    std::string zkHome = flags.build_dir +
      "/third_party/zookeeper-" ZOOKEEPER_VERSION;

    std::string classpath = "-Djava.class.path=" +
      zkHome + "/zookeeper-" ZOOKEEPER_VERSION ".jar:" +
      zkHome + "/lib/log4j-1.2.15.jar";

    LOG(INFO) << "Using classpath setup: " << classpath << std::endl;

    std::vector<std::string> options;
    options.push_back(classpath);
    Try<Jvm*> jvm = Jvm::create(options);
    CHECK_SOME(jvm);

    if (!flags.verbose) {
      // Silence server logs.
      org::apache::log4j::Logger::getRootLogger()
        .setLevel(org::apache::log4j::Level::OFF);

      // Silence client logs.
      // TODO(jsirois): Create C++ ZooKeeper::setLevel.
      zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
    }
  }
}


void ZooKeeperTest::SetUp()
{
  server->startNetwork();
}


ZooKeeperTest::TestWatcher::TestWatcher()
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&mutex, &attr);
  pthread_mutexattr_destroy(&attr);
  pthread_cond_init(&cond, 0);
}


ZooKeeperTest::TestWatcher::~TestWatcher()
{
  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&cond);
}


void ZooKeeperTest::TestWatcher::process(
    ZooKeeper* zk,
    int type,
    int state,
    const std::string& path)
{
  Lock lock(&mutex);
  events.push(Event(type, state, path));
  pthread_cond_signal(&cond);
}


static bool isSessionState(
    const ZooKeeperTest::TestWatcher::Event& event,
    int state)
{
  return event.type == ZOO_SESSION_EVENT && event.state == state;
}


void ZooKeeperTest::TestWatcher::awaitSessionEvent(int state)
{
  awaitEvent(lambda::bind(&isSessionState, lambda::_1, state));
}


static bool isCreated(
    const ZooKeeperTest::TestWatcher::Event& event,
    const std::string& path)
{
  return event.type == ZOO_CHILD_EVENT && event.path == path;
}


void ZooKeeperTest::TestWatcher::awaitCreated(const std::string& path)
{
  awaitEvent(lambda::bind(&isCreated, lambda::_1, path));
}


ZooKeeperTest::TestWatcher::Event
ZooKeeperTest::TestWatcher::awaitEvent()
{
  Lock lock(&mutex);
  while (true) {
    while (events.empty()) {
      pthread_cond_wait(&cond, &mutex);
    }
    Event event = events.front();
    events.pop();
    return event;
  }
}


ZooKeeperTest::TestWatcher::Event
ZooKeeperTest::TestWatcher::awaitEvent(
    const std::tr1::function<bool(Event)>& matches)
{
  while (true) {
    Event event = awaitEvent();
    if (matches(event)) {
      return event;
    }
  }
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {

